#include "wifi_manager.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "app_config.h"
#include "captive_dns.h"
#include "esp_event.h"
#include "esp_idf_version.h"
#include "esp_log.h"
#include "esp_mac.h"
#include "esp_netif.h"
#include "esp_timer.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "lwip/inet.h"
#include "lwip/sockets.h"
#include "sdkconfig.h"

static const char *TAG = "wifi_mgr";

#define AP_STOP_DELAY_US    (60LL * 1000 * 1000)
#define RECONNECT_MAX_MS    30000
#define AP_CLIENT_RETRY_MS  15000

static esp_netif_t *s_sta_netif;
static esp_netif_t *s_ap_netif;
static SemaphoreHandle_t s_lock;
static wifi_mgr_status_t s_st;

static esp_timer_handle_t s_reconnect_timer;
static esp_timer_handle_t s_fallback_timer;
static esp_timer_handle_t s_ap_stop_timer;
static uint32_t s_backoff_ms = 1000;
static bool s_started;

/* ------------------------------------------------------------------------- */

static void apply_ap_config(void)
{
    wifi_config_t ap = { 0 };
    strlcpy((char *)ap.ap.ssid, s_st.ap_ssid, sizeof(ap.ap.ssid));
    ap.ap.ssid_len = (uint8_t)strlen(s_st.ap_ssid);
    /* Open on purpose: joining it is enough, the captive portal then opens the
     * setup page. It only runs while the device has no working Wi-Fi. */
    ap.ap.authmode = WIFI_AUTH_OPEN;
    ap.ap.max_connection = 3;
    ap.ap.channel = 1;
    ap.ap.pmf_cfg.required = false;
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_AP, &ap));
}

static void set_ap_enabled(bool enable)
{
    wifi_mode_t mode;
    if (esp_wifi_get_mode(&mode) != ESP_OK) {
        return;
    }
    bool sta = s_st.sta_configured;
    wifi_mode_t want = enable ? (sta ? WIFI_MODE_APSTA : WIFI_MODE_AP) : WIFI_MODE_STA;
    if (!enable && !sta) {
        return; /* never switch off the only way to reach the device */
    }
    if (mode == want) {
        return;
    }
    ESP_LOGI(TAG, "%s setup access point", enable ? "starting" : "stopping");
    esp_wifi_set_mode(want);
    if (enable) {
        apply_ap_config();
    }
    xSemaphoreTake(s_lock, portMAX_DELAY);
    s_st.ap_active = enable;
    if (!enable) {
        s_st.ap_clients = 0;
    }
    xSemaphoreGive(s_lock);
    if (enable) {
        wifi_mgr_print_ap_credentials();
    }
}

/* ------------------------------------------------------------------------- */
/* Captive portal: DNS                                                       */
/* ------------------------------------------------------------------------- */

/*
 * Answers every name with the AP address while the setup AP runs. Bound to the
 * AP address only, so the home network never sees it. Opens and closes its
 * socket by itself as the AP comes and goes.
 */
static void dns_task(void *arg)
{
    int sock = -1;
    uint32_t bound_ip = 0;
    static uint8_t q[512];
    static uint8_t r[512];
    for (;;) {
        esp_netif_ip_info_t info = { 0 };
        bool want = s_st.ap_active && esp_netif_get_ip_info(s_ap_netif, &info) == ESP_OK && info.ip.addr;
        if (sock >= 0 && (!want || info.ip.addr != bound_ip)) {
            close(sock);
            sock = -1;
            ESP_LOGI(TAG, "captive portal DNS stopped");
        }
        if (!want) {
            vTaskDelay(pdMS_TO_TICKS(1000));
            continue;
        }
        if (sock < 0) {
            sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
            struct sockaddr_in a = { .sin_family = AF_INET, .sin_port = htons(53) };
            a.sin_addr.s_addr = info.ip.addr;
            struct timeval tv = { .tv_sec = 1 };
            if (sock < 0 || bind(sock, (struct sockaddr *)&a, sizeof(a)) != 0) {
                ESP_LOGW(TAG, "captive portal DNS: cannot bind port 53");
                if (sock >= 0) {
                    close(sock);
                    sock = -1;
                }
                vTaskDelay(pdMS_TO_TICKS(5000));
                continue;
            }
            setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
            bound_ip = info.ip.addr;
            ESP_LOGI(TAG, "captive portal DNS running on " IPSTR, IP2STR(&info.ip));
        }
        struct sockaddr_in from;
        socklen_t flen = sizeof(from);
        int n = recvfrom(sock, q, sizeof(q), 0, (struct sockaddr *)&from, &flen);
        if (n <= 0) {
            continue; /* timeout: re-check whether the AP still runs */
        }
        size_t len = captive_dns_reply(q, (size_t)n, bound_ip, r, sizeof(r));
        if (len) {
            sendto(sock, r, len, 0, (struct sockaddr *)&from, flen);
        }
    }
}

/*
 * DHCP for the setup AP: our address as DNS server (so names resolve to the
 * portal) and option 114 (RFC 8910, captive portal URI) for systems that ask
 * for it instead of probing.
 *
 * Called before esp_wifi_start(), while the DHCP server is still in its initial
 * state: options can only be set while it does not run, and esp-netif starts it
 * by itself when the AP comes up - but only from that initial state, so it must
 * not be stopped here. The options stay for every later start of the AP.
 */
static void setup_ap_dhcp(void)
{
    esp_netif_ip_info_t info;
    if (esp_netif_get_ip_info(s_ap_netif, &info) != ESP_OK) {
        return;
    }
    esp_netif_dns_info_t dns = { 0 };
    dns.ip.type = ESP_IPADDR_TYPE_V4;
    dns.ip.u_addr.ip4.addr = info.ip.addr;
    esp_netif_set_dns_info(s_ap_netif, ESP_NETIF_DNS_MAIN, &dns);
    uint8_t offer_dns = 0x02; /* OFFER_DNS */
    esp_netif_dhcps_option(s_ap_netif, ESP_NETIF_OP_SET, ESP_NETIF_DOMAIN_NAME_SERVER, &offer_dns,
                           sizeof(offer_dns));
#if ESP_IDF_VERSION >= ESP_IDF_VERSION_VAL(5, 2, 0)
    static char uri[32];
    snprintf(uri, sizeof(uri), "http://" IPSTR "/", IP2STR(&info.ip));
    esp_err_t err = esp_netif_dhcps_option(s_ap_netif, ESP_NETIF_OP_SET, ESP_NETIF_CAPTIVEPORTAL_URI, uri,
                                           strlen(uri));
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "DHCP captive portal option: %s", esp_err_to_name(err));
    }
#endif
}

static void reconnect_cb(void *arg)
{
    if (s_st.sta_configured) {
        esp_wifi_connect();
    }
}

static void fallback_cb(void *arg)
{
    if (!s_st.sta_connected) {
        ESP_LOGW(TAG, "no Wi-Fi connection after %d s, enabling setup AP for recovery",
                 CONFIG_WIFI_MGR_FALLBACK_AP_DELAY_S);
        set_ap_enabled(true);
    }
}

static void ap_stop_cb(void *arg)
{
    if (s_st.sta_connected) {
        set_ap_enabled(false);
    }
}

static void arm_fallback(void)
{
    if (!esp_timer_is_active(s_fallback_timer)) {
        esp_timer_start_once(s_fallback_timer, (uint64_t)CONFIG_WIFI_MGR_FALLBACK_AP_DELAY_S * 1000000ULL);
    }
}

static void event_handler(void *arg, esp_event_base_t base, int32_t id, void *data)
{
    if (base == WIFI_EVENT) {
        switch (id) {
        case WIFI_EVENT_STA_START:
            if (s_st.sta_configured) {
                esp_wifi_connect();
            }
            break;
        case WIFI_EVENT_STA_CONNECTED:
            ESP_LOGI(TAG, "associated with access point");
            break;
        case WIFI_EVENT_STA_DISCONNECTED: {
            const wifi_event_sta_disconnected_t *ev = data;
            xSemaphoreTake(s_lock, portMAX_DELAY);
            bool was_connected = s_st.sta_connected;
            s_st.sta_connected = false;
            s_st.sta_ip[0] = '\0';
            s_st.last_disconnect_reason = ev->reason;
            xSemaphoreGive(s_lock);
            if (was_connected) {
                ESP_LOGW(TAG, "Wi-Fi connection lost (reason %u)", ev->reason);
                s_backoff_ms = 1000;
            } else {
                ESP_LOGW(TAG, "Wi-Fi connect failed (reason %u), retry in %lu ms", ev->reason,
                         (unsigned long)s_backoff_ms);
            }
            if (s_st.sta_configured) {
                /* Station scans make the soft-AP leave its channel; retry less often while
                 * someone is connected to the setup AP so that the setup page stays usable. */
                uint32_t delay_ms = s_backoff_ms;
                if (s_st.ap_active && s_st.ap_clients > 0 && delay_ms < AP_CLIENT_RETRY_MS) {
                    delay_ms = AP_CLIENT_RETRY_MS;
                }
                esp_timer_stop(s_reconnect_timer);
                esp_timer_start_once(s_reconnect_timer, (uint64_t)delay_ms * 1000ULL);
                s_backoff_ms = s_backoff_ms * 2 > RECONNECT_MAX_MS ? RECONNECT_MAX_MS : s_backoff_ms * 2;
                arm_fallback();
            }
            break;
        }
        case WIFI_EVENT_AP_STACONNECTED:
            xSemaphoreTake(s_lock, portMAX_DELAY);
            s_st.ap_clients++;
            xSemaphoreGive(s_lock);
            ESP_LOGI(TAG, "client joined setup AP");
            break;
        case WIFI_EVENT_AP_STADISCONNECTED:
            xSemaphoreTake(s_lock, portMAX_DELAY);
            if (s_st.ap_clients) {
                s_st.ap_clients--;
            }
            xSemaphoreGive(s_lock);
            break;
        default:
            break;
        }
    } else if (base == IP_EVENT && id == IP_EVENT_STA_GOT_IP) {
        const ip_event_got_ip_t *ev = data;
        xSemaphoreTake(s_lock, portMAX_DELAY);
        s_st.sta_connected = true;
        snprintf(s_st.sta_ip, sizeof(s_st.sta_ip), IPSTR, IP2STR(&ev->ip_info.ip));
        snprintf(s_st.sta_gw, sizeof(s_st.sta_gw), IPSTR, IP2STR(&ev->ip_info.gw));
        xSemaphoreGive(s_lock);
        s_backoff_ms = 1000;
        esp_timer_stop(s_fallback_timer);
        ESP_LOGI(TAG, "connected, IP %s", s_st.sta_ip);
        if (s_st.ap_active) {
            esp_timer_stop(s_ap_stop_timer);
            esp_timer_start_once(s_ap_stop_timer, AP_STOP_DELAY_US);
        }
    }
}

/* ------------------------------------------------------------------------- */

static void apply_sta_config(const char *ssid, const char *pass)
{
    wifi_config_t sta = { 0 };
    strlcpy((char *)sta.sta.ssid, ssid, sizeof(sta.sta.ssid));
    strlcpy((char *)sta.sta.password, pass, sizeof(sta.sta.password));
    sta.sta.threshold.authmode = pass[0] ? WIFI_AUTH_WPA2_PSK : WIFI_AUTH_OPEN;
    sta.sta.pmf_cfg.capable = true;
    sta.sta.pmf_cfg.required = false;
    sta.sta.sae_pwe_h2e = WPA3_SAE_PWE_BOTH;
    sta.sta.scan_method = WIFI_ALL_CHANNEL_SCAN;
    sta.sta.sort_method = WIFI_CONNECT_AP_BY_SIGNAL;
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &sta));
}

esp_err_t wifi_mgr_start(const char *hostname)
{
    if (s_started) {
        return ESP_ERR_INVALID_STATE;
    }
    s_lock = xSemaphoreCreateMutex();
    if (!s_lock) {
        return ESP_ERR_NO_MEM;
    }

    ESP_ERROR_CHECK(esp_netif_init());
    esp_err_t err = esp_event_loop_create_default();
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
        return err;
    }
    s_sta_netif = esp_netif_create_default_wifi_sta();
    s_ap_netif = esp_netif_create_default_wifi_ap();
    strlcpy(s_st.hostname, hostname, sizeof(s_st.hostname));
    esp_netif_set_hostname(s_sta_netif, hostname);

    wifi_init_config_t init = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&init));
    ESP_ERROR_CHECK(esp_wifi_set_storage(WIFI_STORAGE_RAM)); /* credentials live in our own NVS namespace */
    ESP_ERROR_CHECK(esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, event_handler, NULL));
    ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, event_handler, NULL));

    const esp_timer_create_args_t t1 = { .callback = reconnect_cb, .name = "wifi_reconn" };
    const esp_timer_create_args_t t2 = { .callback = fallback_cb, .name = "wifi_fallback" };
    const esp_timer_create_args_t t3 = { .callback = ap_stop_cb, .name = "wifi_apstop" };
    ESP_ERROR_CHECK(esp_timer_create(&t1, &s_reconnect_timer));
    ESP_ERROR_CHECK(esp_timer_create(&t2, &s_fallback_timer));
    ESP_ERROR_CHECK(esp_timer_create(&t3, &s_ap_stop_timer));

    uint8_t mac[6];
    esp_read_mac(mac, ESP_MAC_WIFI_SOFTAP);
    snprintf(s_st.ap_ssid, sizeof(s_st.ap_ssid), "%s-%02X%02X", CONFIG_WIFI_MGR_AP_SSID_PREFIX, mac[4], mac[5]);
    strlcpy(s_st.ap_ip, "192.168.4.1", sizeof(s_st.ap_ip)); /* default of esp_netif_create_default_wifi_ap() */

    char ssid[33] = "";
    char pass[65] = "";
    cfg_get_str(CFG_NS_WIFI, "ssid", ssid, sizeof(ssid));
    cfg_get_str(CFG_NS_WIFI, "pass", pass, sizeof(pass));
    s_st.sta_configured = ssid[0] != '\0';
    strlcpy(s_st.sta_ssid, ssid, sizeof(s_st.sta_ssid));

    if (s_st.sta_configured) {
        ESP_LOGI(TAG, "station mode, SSID \"%s\"", ssid);
        ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
        apply_sta_config(ssid, pass);
        arm_fallback();
    } else {
        ESP_LOGW(TAG, "no Wi-Fi credentials stored, starting setup AP \"%s\"", s_st.ap_ssid);
        ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_AP));
        apply_ap_config();
        s_st.ap_active = true;
    }
    memset(pass, 0, sizeof(pass));
    setup_ap_dhcp();
    ESP_ERROR_CHECK(esp_wifi_start());
    s_started = true;
    xTaskCreate(dns_task, "captive_dns", 3072, NULL, 4, NULL);
    if (s_st.ap_active) {
        wifi_mgr_print_ap_credentials();
    }
    return ESP_OK;
}

bool wifi_mgr_is_started(void)
{
    return s_started;
}

void wifi_mgr_get_status(wifi_mgr_status_t *out)
{
    if (!s_started) {
        memset(out, 0, sizeof(*out));
        return;
    }
    xSemaphoreTake(s_lock, portMAX_DELAY);
    *out = s_st;
    xSemaphoreGive(s_lock);
    wifi_ap_record_t ap;
    if (out->sta_connected && esp_wifi_sta_get_ap_info(&ap) == ESP_OK) {
        out->rssi = ap.rssi;
    }
}

esp_err_t wifi_mgr_set_credentials(const char *ssid, const char *password)
{
    size_t sl = strlen(ssid);
    size_t pl = strlen(password);
    if (sl == 0 || sl > 32 || pl > 64 || (pl > 0 && pl < 8)) {
        return ESP_ERR_INVALID_ARG;
    }
    esp_err_t err = cfg_set_str(CFG_NS_WIFI, "ssid", ssid);
    if (err == ESP_OK) {
        err = cfg_set_str(CFG_NS_WIFI, "pass", password);
    }
    if (err != ESP_OK) {
        return err;
    }
    ESP_LOGI(TAG, "new Wi-Fi credentials stored for SSID \"%s\"", ssid);
    if (!s_started) {
        return ESP_OK; /* used on the next Wi-Fi start */
    }

    xSemaphoreTake(s_lock, portMAX_DELAY);
    s_st.sta_configured = true;
    strlcpy(s_st.sta_ssid, ssid, sizeof(s_st.sta_ssid));
    bool ap = s_st.ap_active;
    xSemaphoreGive(s_lock);

    esp_wifi_set_mode(ap ? WIFI_MODE_APSTA : WIFI_MODE_STA);
    if (ap) {
        apply_ap_config();
    }
    esp_wifi_disconnect();
    apply_sta_config(ssid, password);
    s_backoff_ms = 1000;
    esp_wifi_connect();
    arm_fallback();
    return ESP_OK;
}

esp_err_t wifi_mgr_clear_credentials(void)
{
    cfg_erase_key(CFG_NS_WIFI, "ssid");
    esp_err_t err = cfg_erase_key(CFG_NS_WIFI, "pass");
    ESP_LOGW(TAG, "Wi-Fi credentials erased (takes effect after restart)");
    return err;
}

void wifi_mgr_print_ap_credentials(void)
{
    if (!s_started) {
        printf("Wi-Fi is not running (safe mode). Start it with 'wifi_start'.\n");
        return;
    }
    printf("\n"
           "*****************************************************\n"
           "  Setup Wi-Fi   SSID    : %s\n"
           "                Password: none (open, setup page opens by itself)\n"
           "                URL     : http://%s/\n"
           "*****************************************************\n\n",
           s_st.ap_ssid, s_st.ap_ip);
    fflush(stdout);
}

bool wifi_mgr_is_ap_local_ip(uint32_t ipv4)
{
    esp_netif_ip_info_t info;
    if (!s_st.ap_active || esp_netif_get_ip_info(s_ap_netif, &info) != ESP_OK) {
        return false;
    }
    return info.ip.addr == ipv4;
}

esp_err_t wifi_mgr_scan(wifi_mgr_net_t *out, size_t max, size_t *count)
{
    *count = 0;
    if (!s_started) {
        return ESP_ERR_INVALID_STATE;
    }
    /* Scanning needs the station interface; with the AP alone it is switched on
     * (without connecting - there are no credentials to connect with). */
    wifi_mode_t mode;
    if (esp_wifi_get_mode(&mode) == ESP_OK && mode == WIFI_MODE_AP) {
        esp_wifi_set_mode(WIFI_MODE_APSTA);
    }
    wifi_scan_config_t sc = { .show_hidden = false };
    esp_err_t err = esp_wifi_scan_start(&sc, true);
    if (err != ESP_OK) {
        return err; /* e.g. ESP_ERR_WIFI_STATE while a connection attempt runs */
    }
    uint16_t n = 24;
    wifi_ap_record_t *recs = calloc(n, sizeof(*recs));
    if (!recs) {
        esp_wifi_clear_ap_list();
        return ESP_ERR_NO_MEM;
    }
    err = esp_wifi_scan_get_ap_records(&n, recs);
    for (uint16_t i = 0; err == ESP_OK && i < n; i++) {
        const char *ssid = (const char *)recs[i].ssid;
        if (!ssid[0]) {
            continue;
        }
        /* One entry per name; the list is sorted by signal, so the first is the strongest. */
        bool dup = false;
        for (size_t k = 0; k < *count; k++) {
            dup = dup || strcmp(out[k].ssid, ssid) == 0;
        }
        if (dup || *count >= max) {
            continue;
        }
        strlcpy(out[*count].ssid, ssid, sizeof(out[*count].ssid));
        out[*count].rssi = recs[i].rssi;
        out[*count].open = recs[i].authmode == WIFI_AUTH_OPEN;
        (*count)++;
    }
    free(recs);
    return err;
}
