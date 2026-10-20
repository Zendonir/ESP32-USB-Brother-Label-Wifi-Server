#include "web_server.h"

#include <ctype.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>

#include "diag_log.h"
#include "esp_app_desc.h"
#include "esp_heap_caps.h"
#include "esp_http_server.h"
#include "esp_log.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "lwip/sockets.h"
#include "sdkconfig.h"
#include "strbuf.h"
#include "usb_printer.h"
#include "wifi_manager.h"

static const char *TAG = "web";

#define RESP_BUF_SIZE (24 * 1024)

extern const uint8_t index_html_start[] asm("_binary_index_html_start");
extern const uint8_t index_html_end[] asm("_binary_index_html_end");
extern const uint8_t app_js_start[] asm("_binary_app_js_start");
extern const uint8_t app_js_end[] asm("_binary_app_js_end");
extern const uint8_t style_css_start[] asm("_binary_style_css_start");
extern const uint8_t style_css_end[] asm("_binary_style_css_end");

/* esp_http_server runs all handlers in one task, so one response buffer is enough. */
static char *s_resp;

/* ------------------------------------------------------------------------- */
/* Helpers                                                                   */
/* ------------------------------------------------------------------------- */

static esp_err_t send_static(httpd_req_t *req, const char *type, const uint8_t *start, const uint8_t *end)
{
    httpd_resp_set_type(req, type);
    httpd_resp_set_hdr(req, "Cache-Control", "no-cache");
    /* EMBED_TXTFILES appends a NUL terminator that must not be sent */
    size_t len = (size_t)(end - start);
    if (len > 0 && start[len - 1] == '\0') {
        len--;
    }
    return httpd_resp_send(req, (const char *)start, (ssize_t)len);
}

static esp_err_t send_json(httpd_req_t *req, strbuf_t *sb)
{
    httpd_resp_set_type(req, "application/json");
    httpd_resp_set_hdr(req, "Cache-Control", "no-store");
    if (sb->overflow) {
        ESP_LOGW(TAG, "JSON response truncated (%u bytes)", (unsigned)sb->len);
        return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "response too large");
    }
    return httpd_resp_send(req, sb->buf, (ssize_t)sb->len);
}

static esp_err_t send_json_msg(httpd_req_t *req, const char *status, bool ok, const char *msg)
{
    httpd_resp_set_status(req, status);
    strbuf_t sb;
    sb_init(&sb, s_resp, RESP_BUF_SIZE);
    sb_printf(&sb, "{\"ok\":%s,\"message\":", ok ? "true" : "false");
    sb_json_str(&sb, msg);
    sb_putc(&sb, '}');
    return send_json(req, &sb);
}

/* True if the request arrived through the setup access point (local address 192.168.4.1). */
static bool request_via_setup_ap(httpd_req_t *req)
{
    int fd = httpd_req_to_sockfd(req);
    struct sockaddr_storage addr;
    socklen_t len = sizeof(addr);
    if (getsockname(fd, (struct sockaddr *)&addr, &len) != 0) {
        return false;
    }
    uint32_t ip = 0;
    if (addr.ss_family == AF_INET) {
        ip = ((struct sockaddr_in *)&addr)->sin_addr.s_addr;
    }
#if CONFIG_LWIP_IPV6
    else if (addr.ss_family == AF_INET6) {
        const struct sockaddr_in6 *a6 = (const struct sockaddr_in6 *)&addr;
        const uint8_t *b = (const uint8_t *)&a6->sin6_addr;
        static const uint8_t v4mapped[12] = { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0xff, 0xff };
        if (memcmp(b, v4mapped, sizeof(v4mapped)) != 0) {
            return false;
        }
        memcpy(&ip, b + 12, 4);
    }
#endif
    return ip != 0 && wifi_mgr_is_ap_local_ip(ip);
}

static int hexval(char c)
{
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

/* In-place application/x-www-form-urlencoded decoding. Returns false on malformed input. */
static bool url_decode(char *s)
{
    char *o = s;
    for (char *p = s; *p; p++) {
        if (*p == '+') {
            *o++ = ' ';
        } else if (*p == '%') {
            int h = hexval(p[1]);
            int l = h >= 0 ? hexval(p[2]) : -1;
            if (h < 0 || l < 0) {
                return false;
            }
            *o++ = (char)((h << 4) | l);
            p += 2;
        } else {
            *o++ = *p;
        }
    }
    *o = '\0';
    return true;
}

static int recv_body(httpd_req_t *req, char *buf, size_t size)
{
    if (req->content_len >= size) {
        return -1;
    }
    size_t got = 0;
    while (got < req->content_len) {
        int r = httpd_req_recv(req, buf + got, req->content_len - got);
        if (r == HTTPD_SOCK_ERR_TIMEOUT) {
            continue;
        }
        if (r <= 0) {
            return -1;
        }
        got += (size_t)r;
    }
    buf[got] = '\0';
    return (int)got;
}

static const char *reset_reason_name(esp_reset_reason_t r)
{
    switch (r) {
    case ESP_RST_POWERON:   return "power_on";
    case ESP_RST_EXT:       return "external";
    case ESP_RST_SW:        return "software";
    case ESP_RST_PANIC:     return "panic";
    case ESP_RST_INT_WDT:   return "interrupt_watchdog";
    case ESP_RST_TASK_WDT:  return "task_watchdog";
    case ESP_RST_WDT:       return "other_watchdog";
    case ESP_RST_DEEPSLEEP: return "deep_sleep";
    case ESP_RST_BROWNOUT:  return "brownout";
    case ESP_RST_SDIO:      return "sdio";
    case ESP_RST_USB:       return "usb";
    case ESP_RST_JTAG:      return "jtag";
    default:                return "unknown";
    }
}

/* ------------------------------------------------------------------------- */
/* Handlers                                                                  */
/* ------------------------------------------------------------------------- */

static esp_err_t h_index(httpd_req_t *req)
{
    return send_static(req, "text/html; charset=utf-8", index_html_start, index_html_end);
}

static esp_err_t h_app_js(httpd_req_t *req)
{
    return send_static(req, "application/javascript; charset=utf-8", app_js_start, app_js_end);
}

static esp_err_t h_style_css(httpd_req_t *req)
{
    return send_static(req, "text/css; charset=utf-8", style_css_start, style_css_end);
}

static esp_err_t h_health(httpd_req_t *req)
{
    strbuf_t sb;
    sb_init(&sb, s_resp, RESP_BUF_SIZE);
    usb_printer_summary_t p;
    usb_printer_get_summary(&p);
    sb_printf(&sb, "{\"status\":\"ok\",\"uptime_s\":%lld,\"printer_ready\":%s}",
              esp_timer_get_time() / 1000000, p.ready ? "true" : "false");
    return send_json(req, &sb);
}

static esp_err_t h_status(httpd_req_t *req)
{
    strbuf_t sb;
    sb_init(&sb, s_resp, RESP_BUF_SIZE);
    const esp_app_desc_t *app = esp_app_get_description();
    wifi_mgr_status_t w;
    wifi_mgr_get_status(&w);
    usb_printer_summary_t p;
    usb_printer_get_summary(&p);

    sb_puts(&sb, "{\"firmware\":{\"name\":");
    sb_json_str(&sb, app->project_name);
    sb_puts(&sb, ",\"version\":");
    sb_json_str(&sb, app->version);
    sb_puts(&sb, ",\"idf\":");
    sb_json_str(&sb, app->idf_ver);
    sb_printf(&sb, ",\"build\":\"%s %s\",\"phase\":\"2-usb-diagnostics\"},", app->date, app->time);

    sb_printf(&sb, "\"uptime_s\":%lld,\"reset_reason\":\"%s\",", esp_timer_get_time() / 1000000,
              reset_reason_name(esp_reset_reason()));
    sb_printf(&sb, "\"memory\":{\"internal_free\":%u,\"internal_min_free\":%u,\"internal_largest_block\":%u,"
              "\"psram_free\":%u,\"psram_total\":%u},",
              (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL),
              (unsigned)heap_caps_get_minimum_free_size(MALLOC_CAP_INTERNAL),
              (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL),
              (unsigned)heap_caps_get_free_size(MALLOC_CAP_SPIRAM),
              (unsigned)heap_caps_get_total_size(MALLOC_CAP_SPIRAM));

    sb_printf(&sb, "\"wifi\":{\"sta_configured\":%s,\"sta_connected\":%s,\"ssid\":",
              w.sta_configured ? "true" : "false", w.sta_connected ? "true" : "false");
    sb_json_str(&sb, w.sta_ssid);
    sb_puts(&sb, ",\"ip\":");
    sb_json_str(&sb, w.sta_ip);
    sb_puts(&sb, ",\"gateway\":");
    sb_json_str(&sb, w.sta_gw);
    sb_printf(&sb, ",\"rssi\":%d,\"last_disconnect_reason\":%u,\"ap_active\":%s,\"ap_ssid\":",
              w.rssi, w.last_disconnect_reason, w.ap_active ? "true" : "false");
    sb_json_str(&sb, w.ap_ssid);
    sb_printf(&sb, ",\"ap_ip\":\"%s\",\"ap_clients\":%u,\"hostname\":", w.ap_ip, w.ap_clients);
    sb_json_str(&sb, w.hostname);
    sb_printf(&sb, ",\"via_setup_ap\":%s},", request_via_setup_ap(req) ? "true" : "false");

    sb_printf(&sb, "\"printer\":{\"connected\":%s,\"ready\":%s,\"is_printer_class\":%s,"
              "\"vid\":\"0x%04x\",\"pid\":\"0x%04x\",\"manufacturer\":",
              p.connected ? "true" : "false", p.ready ? "true" : "false",
              p.is_printer_class ? "true" : "false", p.vid, p.pid);
    sb_json_str(&sb, p.manufacturer);
    sb_puts(&sb, ",\"product\":");
    sb_json_str(&sb, p.product);
    sb_printf(&sb, ",\"connect_count\":%lu,\"bytes_written_total\":%lu,\"write_errors_total\":%lu,\"last_error\":",
              (unsigned long)p.connect_count, (unsigned long)p.bytes_written_total,
              (unsigned long)p.write_errors_total);
    sb_json_str(&sb, p.last_error);
    sb_puts(&sb, "},");

#if CONFIG_DIAG_ENABLE_RAW_SEND
    sb_printf(&sb, "\"features\":{\"raw_send\":true,\"raw_send_max_bytes\":%d}}", CONFIG_DIAG_RAW_SEND_MAX_BYTES);
#else
    sb_puts(&sb, "\"features\":{\"raw_send\":false}}");
#endif
    return send_json(req, &sb);
}

static esp_err_t h_usb(httpd_req_t *req)
{
    strbuf_t sb;
    sb_init(&sb, s_resp, RESP_BUF_SIZE);
    usb_printer_report_json(&sb);
    return send_json(req, &sb);
}

static esp_err_t h_usb_text(httpd_req_t *req)
{
    strbuf_t sb;
    sb_init(&sb, s_resp, RESP_BUF_SIZE);
    usb_printer_report_text(&sb);
    httpd_resp_set_type(req, "text/plain; charset=utf-8");
    httpd_resp_set_hdr(req, "Cache-Control", "no-store");
    return httpd_resp_send(req, sb.buf, (ssize_t)sb.len);
}

static esp_err_t h_usb_refresh(httpd_req_t *req)
{
    esp_err_t err = usb_printer_refresh_class_info();
    if (err != ESP_OK) {
        return send_json_msg(req, "409 Conflict", false, "no USB device connected");
    }
    return h_usb(req);
}

static esp_err_t h_log(httpd_req_t *req)
{
    size_t n = diag_log_read(s_resp, RESP_BUF_SIZE);
    httpd_resp_set_type(req, "text/plain; charset=utf-8");
    httpd_resp_set_hdr(req, "Cache-Control", "no-store");
    return httpd_resp_send(req, s_resp, (ssize_t)n);
}

static esp_err_t h_wifi_set(httpd_req_t *req)
{
    if (!request_via_setup_ap(req)) {
        return send_json_msg(req, "403 Forbidden", false,
                             "Wi-Fi settings can only be changed while connected to the setup access point");
    }
    char body[256];
    if (recv_body(req, body, sizeof(body)) < 0) {
        return send_json_msg(req, "400 Bad Request", false, "body missing or too large");
    }
    char ssid[96] = "";
    char pass[200] = "";
    httpd_query_key_value(body, "ssid", ssid, sizeof(ssid));
    httpd_query_key_value(body, "password", pass, sizeof(pass));
    memset(body, 0, sizeof(body));
    esp_err_t err = ESP_ERR_INVALID_ARG;
    if (url_decode(ssid) && url_decode(pass)) {
        err = wifi_mgr_set_credentials(ssid, pass);
    }
    memset(pass, 0, sizeof(pass));
    if (err == ESP_ERR_INVALID_ARG) {
        return send_json_msg(req, "400 Bad Request", false,
                             "SSID 1-32 bytes, password empty (open network) or 8-64 characters");
    }
    if (err != ESP_OK) {
        return send_json_msg(req, "500 Internal Server Error", false, esp_err_to_name(err));
    }
    return send_json_msg(req, "200 OK", true, "credentials stored, connecting");
}

static void restart_task(void *arg)
{
    vTaskDelay(pdMS_TO_TICKS(500));
    esp_restart();
}

static esp_err_t h_restart(httpd_req_t *req)
{
    if (!request_via_setup_ap(req)) {
        return send_json_msg(req, "403 Forbidden", false, "restart is only allowed via the setup access point "
                             "or the serial console in this firmware phase");
    }
    send_json_msg(req, "200 OK", true, "restarting");
    xTaskCreate(restart_task, "restart", 2048, NULL, 5, NULL);
    return ESP_OK;
}

#if CONFIG_DIAG_ENABLE_RAW_SEND
/*
 * Forwards the request body unchanged to the printer's bulk OUT endpoint.
 * Development aid for phase 3 (e.g. a .prn file produced by the original
 * Brother driver via "print to file"). Bypasses any queue - one job at a time.
 */
static esp_err_t h_diag_raw(httpd_req_t *req)
{
    if (req->content_len == 0 || req->content_len > CONFIG_DIAG_RAW_SEND_MAX_BYTES) {
        return send_json_msg(req, "413 Payload Too Large", false, "empty body or larger than DIAG_RAW_SEND_MAX_BYTES");
    }
    usb_printer_summary_t p;
    usb_printer_get_summary(&p);
    if (!p.ready) {
        return send_json_msg(req, "409 Conflict", false, "printer not ready");
    }
    ESP_LOGI(TAG, "raw send: %u bytes", (unsigned)req->content_len);
    static uint8_t chunk[4096];
    size_t received = 0;
    size_t acked = 0;
    esp_err_t usb_err = ESP_OK;
    bool net_err = false;
    while (received < req->content_len) {
        size_t want = req->content_len - received;
        int r = httpd_req_recv(req, (char *)chunk, want < sizeof(chunk) ? want : sizeof(chunk));
        if (r == HTTPD_SOCK_ERR_TIMEOUT) {
            continue;
        }
        if (r <= 0) {
            net_err = true;
            break;
        }
        received += (size_t)r;
        size_t w = 0;
        usb_err = usb_printer_write(chunk, (size_t)r, CONFIG_DIAG_RAW_SEND_TIMEOUT_MS, &w);
        acked += w;
        if (usb_err != ESP_OK) {
            break;
        }
    }
    if (net_err) {
        ESP_LOGE(TAG, "raw send: connection lost after %u of %u bytes; printer received an INCOMPLETE job",
                 (unsigned)received, (unsigned)req->content_len);
        return ESP_FAIL;
    }
    strbuf_t sb;
    sb_init(&sb, s_resp, RESP_BUF_SIZE);
    bool ok = usb_err == ESP_OK && acked == req->content_len;
    sb_printf(&sb, "{\"ok\":%s,\"bytes_expected\":%u,\"bytes_received\":%u,\"bytes_acknowledged_by_usb\":%u,"
              "\"usb_result\":\"%s\",\"note\":\"USB acknowledgement does not confirm a physical print\"}",
              ok ? "true" : "false", (unsigned)req->content_len, (unsigned)received, (unsigned)acked,
              esp_err_to_name(usb_err));
    if (!ok) {
        httpd_resp_set_status(req, "502 Bad Gateway");
    }
    ESP_LOGI(TAG, "raw send finished: %u/%u bytes acknowledged (%s)", (unsigned)acked,
             (unsigned)req->content_len, esp_err_to_name(usb_err));
    return send_json(req, &sb);
}
#endif

/* ------------------------------------------------------------------------- */

esp_err_t web_server_start(void)
{
    s_resp = heap_caps_malloc(RESP_BUF_SIZE, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!s_resp) {
        s_resp = malloc(RESP_BUF_SIZE);
    }
    if (!s_resp) {
        return ESP_ERR_NO_MEM;
    }

    httpd_config_t cfg = HTTPD_DEFAULT_CONFIG();
    cfg.stack_size = 8192;
    cfg.max_uri_handlers = 16;
    cfg.max_open_sockets = 5;
    cfg.lru_purge_enable = true;
    cfg.recv_wait_timeout = 10;
    cfg.send_wait_timeout = 10;

    httpd_handle_t srv = NULL;
    esp_err_t err = httpd_start(&srv, &cfg);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "httpd_start: %s", esp_err_to_name(err));
        return err;
    }

    const httpd_uri_t uris[] = {
        { .uri = "/",                .method = HTTP_GET,  .handler = h_index },
        { .uri = "/app.js",          .method = HTTP_GET,  .handler = h_app_js },
        { .uri = "/style.css",       .method = HTTP_GET,  .handler = h_style_css },
        { .uri = "/api/health",      .method = HTTP_GET,  .handler = h_health },
        { .uri = "/api/status",      .method = HTTP_GET,  .handler = h_status },
        { .uri = "/api/usb",         .method = HTTP_GET,  .handler = h_usb },
        { .uri = "/api/usb/text",    .method = HTTP_GET,  .handler = h_usb_text },
        { .uri = "/api/usb/refresh", .method = HTTP_POST, .handler = h_usb_refresh },
        { .uri = "/api/log",         .method = HTTP_GET,  .handler = h_log },
        { .uri = "/api/wifi",        .method = HTTP_POST, .handler = h_wifi_set },
        { .uri = "/api/system/restart", .method = HTTP_POST, .handler = h_restart },
#if CONFIG_DIAG_ENABLE_RAW_SEND
        { .uri = "/api/diag/raw",    .method = HTTP_POST, .handler = h_diag_raw },
#endif
    };
    for (size_t i = 0; i < sizeof(uris) / sizeof(uris[0]); i++) {
        httpd_register_uri_handler(srv, &uris[i]);
    }
    ESP_LOGI(TAG, "HTTP server listening on port %d", cfg.server_port);
    return ESP_OK;
}
