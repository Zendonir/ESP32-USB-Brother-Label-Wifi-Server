#include "web_server.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>

#include "brother_printer.h"
#include "diag_log.h"
#include "print_queue.h"
#include "raw_tcp_server.h"
#include "esp_app_desc.h"
#include "esp_heap_caps.h"
#include "esp_http_server.h"
#include "esp_log.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "lwip/inet.h"
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
/* /api/diag/raw is on by default (like port 9100 later, it accepts print data from the
 * local network); 'raw_send off' on the serial console disables it until restart. */
static volatile bool s_raw_send_enabled = true;

void web_server_set_raw_send(bool enable)
{
    s_raw_send_enabled = enable;
    ESP_LOGW(TAG, "raw send endpoint %s", enable ? "enabled" : "DISABLED (until restart)");
}

bool web_server_raw_send_enabled(void)
{
    return s_raw_send_enabled;
}

static void json_brother_status(strbuf_t *sb, const brother_status_t *st, int64_t age_ms)
{
    sb_printf(sb, "{\"age_ms\":%lld,\"status\":", (long long)age_ms);
    brother_status_format_json(st, sb);
    sb_putc(sb, '}');
}

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
    sb_printf(&sb, ",\"build\":\"%s %s\",\"phase\":\"4-raw9100-queue\"},", app->date, app->time);

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

    print_queue_stats_t qs;
    print_queue_get_stats(&qs);
    char allow[24];
    raw_tcp_server_get_allowed(allow, sizeof(allow));
    sb_printf(&sb, "\"queue\":{\"queued\":%lu,\"active_id\":%lu,\"total_printed\":%lu,\"total_failed\":%lu,"
              "\"spool_bytes\":%u,\"raw_port\":%d,\"raw_connections\":%lu,\"raw_allowed\":",
              (unsigned long)qs.queued, (unsigned long)qs.active_id, (unsigned long)qs.total_printed,
              (unsigned long)qs.total_failed, (unsigned)qs.spool_bytes, CONFIG_RAW_TCP_PORT,
              (unsigned long)raw_tcp_server_connections());
    sb_json_str(&sb, allow);
    sb_puts(&sb, "},");

    brother_status_t bst;
    int64_t age_ms = 0;
    sb_puts(&sb, "\"brother\":");
    if (brother_get_last_status(&bst, &age_ms)) {
        json_brother_status(&sb, &bst, age_ms);
    } else {
        sb_puts(&sb, "null");
    }
    sb_putc(&sb, ',');

    /* What senders such as the Lebensmittel-Server check before using the print API:
     * job_key = X-Job-Key is honoured (no duplicate label after a repeated request). */
    sb_printf(&sb, "\"print_api\":{\"version\":1,\"job_name\":true,\"job_key\":true,\"max_job_bytes\":%d},",
              CONFIG_PRINT_QUEUE_MAX_JOB_BYTES);

#if CONFIG_DIAG_ENABLE_RAW_SEND
    sb_printf(&sb, "\"features\":{\"raw_send\":%s,\"raw_send_available\":true,\"raw_send_max_bytes\":%d}}",
              s_raw_send_enabled ? "true" : "false", CONFIG_PRINT_QUEUE_MAX_JOB_BYTES);
#else
    sb_puts(&sb, "\"features\":{\"raw_send\":false,\"raw_send_available\":false}}");
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

/* GET /api/printer/status        -> runs the Brother status request (ESC i S)
 * GET /api/printer/status?cached=1 -> last status received, no printer access */
static esp_err_t h_printer_status(httpd_req_t *req)
{
    char q[32] = "";
    bool cached = false;
    if (httpd_req_get_url_query_str(req, q, sizeof(q)) == ESP_OK) {
        char v[4];
        cached = httpd_query_key_value(q, "cached", v, sizeof(v)) == ESP_OK && v[0] == '1';
    }
    brother_status_t st;
    int64_t age_ms = 0;
    if (!cached) {
        esp_err_t err = brother_query_status(&st);
        if (err == ESP_ERR_INVALID_STATE) {
            return send_json_msg(req, "409 Conflict", false, "printer not connected or busy with a job");
        }
        if (err != ESP_OK) {
            return send_json_msg(req, "504 Gateway Timeout", false, "printer did not answer the status request");
        }
    } else if (!brother_get_last_status(&st, &age_ms)) {
        return send_json_msg(req, "404 Not Found", false, "no status received yet");
    }
    strbuf_t sb;
    sb_init(&sb, s_resp, RESP_BUF_SIZE);
    sb_puts(&sb, "{\"ok\":true,");
    sb_json_key(&sb, "printer");
    json_brother_status(&sb, &st, age_ms);
    sb_putc(&sb, '}');
    return send_json(req, &sb);
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

/* Receives the request body into a spool. Returns NULL on success, else a reason. */
static const char *receive_job(httpd_req_t *req, print_spool_t *sp, bool *connection_lost)
{
    static uint8_t chunk[4096];
    size_t received = 0;
    *connection_lost = false;
    while (received < req->content_len) {
        size_t want = req->content_len - received;
        int r = httpd_req_recv(req, (char *)chunk, want < sizeof(chunk) ? want : sizeof(chunk));
        if (r == HTTPD_SOCK_ERR_TIMEOUT) {
            continue;
        }
        if (r <= 0) {
            *connection_lost = true;
            return "connection lost during upload";
        }
        received += (size_t)r;
        if (print_spool_append(sp, chunk, (size_t)r) != ESP_OK) {
            return "out of memory while receiving";
        }
    }
    return NULL;
}

/*
 * Optional request header, reduced to printable ASCII (it ends up in JSON and
 * on the serial console). Returns false if the header is missing.
 */
static bool header_ascii(httpd_req_t *req, const char *field, char *out, size_t size)
{
    out[0] = '\0';
    size_t len = httpd_req_get_hdr_value_len(req, field);
    if (len == 0) {
        return false;
    }
    char buf[96];
    if (httpd_req_get_hdr_value_str(req, field, buf, sizeof(buf)) == ESP_ERR_NOT_FOUND) {
        return false;
    }
    size_t n = 0;
    for (const char *p = buf; *p && n + 1 < size; p++) {
        out[n++] = (*p >= 0x20 && *p < 0x7f) ? *p : '?';
    }
    out[n] = '\0';
    return n > 0;
}

/* Reads and drops the request body (a repeated job whose data is not needed). */
static void discard_body(httpd_req_t *req)
{
    static uint8_t sink[1024];
    size_t left = req->content_len;
    while (left > 0) {
        int r = httpd_req_recv(req, (char *)sink, left < sizeof(sink) ? left : sizeof(sink));
        if (r == HTTPD_SOCK_ERR_TIMEOUT) {
            continue;
        }
        if (r <= 0) {
            return;
        }
        left -= (size_t)r;
    }
}

/*
 * Common part of POST /api/print/raw and /api/diag/raw: spool and queue the body.
 *
 * Headers (both optional):
 *   X-Job-Name  shown in the job list, e.g. "Etikett LEB000123"
 *   X-Job-Key   idempotency key. A sender that did not get an answer (Wi-Fi
 *               dropped after the upload) repeats the request with the same
 *               key and gets the first job back instead of a second label.
 * *duplicate is true when the key belonged to an existing job; nothing new is queued.
 */
static esp_err_t submit_from_request(httpd_req_t *req, uint32_t *id, bool *duplicate)
{
    *duplicate = false;
    char name[sizeof(((print_job_info_t *)0)->name)];
    char key[sizeof(((print_job_info_t *)0)->key)];
    header_ascii(req, "X-Job-Name", name, sizeof(name));
    header_ascii(req, "X-Job-Key", key, sizeof(key));
    if (key[0] && print_queue_find_key(key, id) == ESP_OK) {
        discard_body(req);
        *duplicate = true;
        ESP_LOGI(TAG, "job key %s already known as job %lu, not queued again", key, (unsigned long)*id);
        return ESP_OK;
    }
    if (req->content_len == 0 || req->content_len > CONFIG_PRINT_QUEUE_MAX_JOB_BYTES) {
        send_json_msg(req, "413 Payload Too Large", false, "empty body or larger than the maximum job size");
        return ESP_FAIL;
    }
    char client[16] = "";
    int fd = httpd_req_to_sockfd(req);
    struct sockaddr_storage addr;
    socklen_t alen = sizeof(addr);
    if (getpeername(fd, (struct sockaddr *)&addr, &alen) == 0) {
        if (addr.ss_family == AF_INET) {
            inet_ntoa_r(((struct sockaddr_in *)&addr)->sin_addr, client, sizeof(client));
        }
#if CONFIG_LWIP_IPV6
        else if (addr.ss_family == AF_INET6) {
            const uint8_t *b = (const uint8_t *)&((struct sockaddr_in6 *)&addr)->sin6_addr;
            snprintf(client, sizeof(client), "%u.%u.%u.%u", b[12], b[13], b[14], b[15]);
        }
#endif
    }
    print_spool_t sp = { 0 };
    bool lost = false;
    const char *fail = receive_job(req, &sp, &lost);
    if (fail) {
        size_t n = sp.len;
        print_spool_free(&sp);
        print_queue_record_incomplete(JOB_SRC_HTTP, client, n, fail);
        if (!lost) {
            send_json_msg(req, "507 Insufficient Storage", false, fail);
        }
        return ESP_FAIL;
    }
    if (print_queue_submit(&sp, JOB_SRC_HTTP, client, name, key, id) != ESP_OK) {
        send_json_msg(req, "503 Service Unavailable", false, "print queue full");
        return ESP_FAIL;
    }
    return ESP_OK;
}

static esp_err_t send_job_ex(httpd_req_t *req, const char *status, uint32_t id, bool duplicate)
{
    print_job_info_t j;
    if (print_queue_get(id, &j) != ESP_OK) {
        return send_json_msg(req, "404 Not Found", false, "job not found");
    }
    strbuf_t sb;
    sb_init(&sb, s_resp, RESP_BUF_SIZE);
    sb_printf(&sb, "{\"ok\":%s,\"job_id\":%lu,\"result\":\"%s\",\"duplicate\":%s,\"job\":",
              (j.state == JOB_FAILED || j.state == JOB_INCOMPLETE || j.state == JOB_CANCELLED) ? "false" : "true",
              (unsigned long)id, print_job_state_name(j.state), duplicate ? "true" : "false");
    print_queue_job_json(&j, &sb);
    sb_putc(&sb, '}');
    if (status) {
        httpd_resp_set_status(req, status);
    }
    return send_json(req, &sb);
}

static esp_err_t send_job(httpd_req_t *req, const char *status, uint32_t id)
{
    return send_job_ex(req, status, id, false);
}

/*
 * POST /api/print/raw: queue printer-ready data, answer immediately (202 + job id).
 * A repeated X-Job-Key answers 200 with the existing job ("duplicate": true).
 */
static esp_err_t h_print_raw(httpd_req_t *req)
{
    uint32_t id = 0;
    bool duplicate = false;
    if (submit_from_request(req, &id, &duplicate) != ESP_OK) {
        return ESP_OK; /* response already sent (or connection gone) */
    }
    return send_job_ex(req, duplicate ? "200 OK" : "202 Accepted", id, duplicate);
}

#if CONFIG_DIAG_ENABLE_RAW_SEND
/*
 * POST /api/diag/raw: like /api/print/raw but waits until the job has finished
 * (used by the test print in the web interface).
 */
static esp_err_t h_diag_raw(httpd_req_t *req)
{
    if (!s_raw_send_enabled) {
        return send_json_msg(req, "403 Forbidden", false,
                             "raw send was disabled on the serial console ('raw_send on' enables it again)");
    }
    uint32_t id = 0;
    bool duplicate = false;
    if (submit_from_request(req, &id, &duplicate) != ESP_OK) {
        return ESP_OK;
    }
    print_job_info_t j;
    print_queue_wait(id, 180000, &j);
    return send_job(req, NULL, id);
}
#endif

/* GET /api/jobs */
static esp_err_t h_jobs(httpd_req_t *req)
{
    strbuf_t sb;
    sb_init(&sb, s_resp, RESP_BUF_SIZE);
    print_queue_list_json(&sb);
    return send_json(req, &sb);
}

/* GET /api/jobs/{id}, DELETE /api/jobs/{id}, POST /api/jobs/{id}/retry */
static esp_err_t h_job(httpd_req_t *req)
{
    const char *p = req->uri + strlen("/api/jobs/");
    char *end;
    unsigned long id = strtoul(p, &end, 10);
    bool retry = strncmp(end, "/retry", 6) == 0 && (end[6] == '\0' || end[6] == '?');
    bool plain = *end == '\0' || *end == '?';
    if (end == p || (!plain && !retry)) {
        return send_json_msg(req, "404 Not Found", false, "unknown job URL");
    }
    if (req->method == HTTP_GET && plain) {
        return send_job(req, NULL, (uint32_t)id);
    }
    if (req->method == HTTP_DELETE && plain) {
        esp_err_t err = print_queue_cancel((uint32_t)id);
        if (err == ESP_ERR_NOT_FOUND) {
            return send_json_msg(req, "404 Not Found", false, "job not found");
        }
        if (err != ESP_OK) {
            return send_json_msg(req, "409 Conflict", false, "only queued or waiting jobs can be cancelled");
        }
        return send_job(req, NULL, (uint32_t)id);
    }
    if (req->method == HTTP_POST && retry) {
        esp_err_t err = print_queue_retry((uint32_t)id);
        if (err == ESP_ERR_NOT_FOUND) {
            return send_json_msg(req, "404 Not Found", false, "job not found");
        }
        if (err != ESP_OK) {
            return send_json_msg(req, "409 Conflict", false,
                                 "only failed, unknown or cancelled jobs whose data is still kept can be retried");
        }
        return send_job(req, "202 Accepted", (uint32_t)id);
    }
    return send_json_msg(req, "405 Method Not Allowed", false, "method not allowed for this URL");
}

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
    cfg.max_uri_handlers = 24;
    cfg.uri_match_fn = httpd_uri_match_wildcard;
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
        { .uri = "/api/printer/status", .method = HTTP_GET, .handler = h_printer_status },
        { .uri = "/api/print/raw",   .method = HTTP_POST, .handler = h_print_raw },
        { .uri = "/api/jobs",        .method = HTTP_GET,  .handler = h_jobs },
        { .uri = "/api/jobs/*",      .method = HTTP_GET,  .handler = h_job },
        { .uri = "/api/jobs/*",      .method = HTTP_DELETE, .handler = h_job },
        { .uri = "/api/jobs/*",      .method = HTTP_POST, .handler = h_job },
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
