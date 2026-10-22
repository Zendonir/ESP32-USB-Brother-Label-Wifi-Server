#include "brother_printer.h"

#include <string.h>

#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "usb_printer.h"

static const char *TAG = "brother";

#define QUERY_WRITE_TIMEOUT_MS  2000
#define QUERY_REPLY_TIMEOUT_MS  1000
#define QUERY_REPLY_TRIES       4
#define DRAIN_TIMEOUT_MS        20
#define DRAIN_MAX               8
#define NEXT_PAGE_WAIT_MS       3000

static portMUX_TYPE s_mux = portMUX_INITIALIZER_UNLOCKED;
static brother_status_t s_last;
static int64_t s_last_time_us;
static bool s_have_last;

static void remember(const brother_status_t *st)
{
    taskENTER_CRITICAL(&s_mux);
    s_last = *st;
    s_last_time_us = esp_timer_get_time();
    s_have_last = true;
    taskEXIT_CRITICAL(&s_mux);
}

bool brother_get_last_status(brother_status_t *out, int64_t *age_ms)
{
    taskENTER_CRITICAL(&s_mux);
    bool have = s_have_last;
    if (have) {
        *out = s_last;
        if (age_ms) {
            *age_ms = (esp_timer_get_time() - s_last_time_us) / 1000;
        }
    }
    taskEXIT_CRITICAL(&s_mux);
    return have;
}

static void log_status(const char *what, const brother_status_t *st)
{
    char buf[96];
    strbuf_t sb;
    sb_init(&sb, buf, sizeof(buf));
    brother_status_errors_text(st, &sb);
    ESP_LOGI(TAG, "%s: %s, phase %s, media %s %umm, errors: %s", what, brother_status_type_name(st->status_type),
             brother_phase_name(st->phase_type), brother_media_type_name(st->media_type), st->media_width_mm, buf);
}

/* Reads one status message. ESP_ERR_TIMEOUT if none arrived, ESP_ERR_INVALID_RESPONSE for garbage. */
static esp_err_t read_one(brother_status_t *st, uint32_t timeout_ms)
{
    uint8_t buf[64];
    size_t got = 0;
    esp_err_t err = usb_printer_read(buf, sizeof(buf), &got, timeout_ms);
    if (err != ESP_OK) {
        return err;
    }
    if (got == 0) {
        return ESP_ERR_TIMEOUT;
    }
    if (!brother_status_parse(buf, got, st)) {
        ESP_LOGW(TAG, "unexpected %u bytes on bulk IN (first byte 0x%02x)", (unsigned)got, buf[0]);
        return ESP_ERR_INVALID_RESPONSE;
    }
    remember(st);
    return ESP_OK;
}

esp_err_t brother_query_status(brother_status_t *out)
{
    if (usb_printer_job_begin(0) != ESP_OK) {
        return ESP_ERR_INVALID_STATE; /* a job is running: status requests are not allowed while printing */
    }
    usb_printer_summary_t sum;
    usb_printer_get_summary(&sum);
    esp_err_t err = ESP_ERR_INVALID_STATE;
    if (!sum.ready) {
        goto out;
    }

    /* Drop messages the printer sent earlier on its own so they are not taken for the reply. */
    brother_status_t st;
    for (int i = 0; i < DRAIN_MAX; i++) {
        esp_err_t e = read_one(&st, DRAIN_TIMEOUT_MS);
        if (e == ESP_OK) {
            log_status("earlier message", &st);
        } else if (e != ESP_ERR_INVALID_RESPONSE) {
            break;
        }
    }

    uint8_t query[BROTHER_STATUS_QUERY_LEN];
    size_t n = brother_build_status_query(query, sizeof(query));
    if (n == 0) {
        err = ESP_ERR_INVALID_SIZE;
        goto out;
    }
    size_t written = 0;
    err = usb_printer_write(query, n, QUERY_WRITE_TIMEOUT_MS, &written);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "status request not sent: %s (%u of %u bytes)", esp_err_to_name(err),
                 (unsigned)written, (unsigned)n);
        goto out;
    }

    err = ESP_ERR_TIMEOUT;
    for (int i = 0; i < QUERY_REPLY_TRIES; i++) {
        esp_err_t e = read_one(&st, QUERY_REPLY_TIMEOUT_MS);
        if (e == ESP_OK) {
            if (st.status_type == BROTHER_ST_REPLY) {
                log_status("status", &st);
                *out = st;
                err = ESP_OK;
                break;
            }
            log_status("message while waiting for reply", &st);
        } else if (e != ESP_ERR_TIMEOUT && e != ESP_ERR_INVALID_RESPONSE) {
            err = e;
            break;
        }
    }
    if (err == ESP_ERR_TIMEOUT) {
        ESP_LOGW(TAG, "no reply to status request");
    }
out:
    usb_printer_job_end();
    return err;
}

void brother_collect_statuses(uint32_t total_ms, uint32_t idle_ms, uint32_t first_ms, brother_status_log_t *log)
{
    memset(log, 0, sizeof(*log));
    int64_t start = esp_timer_get_time();
    int64_t end = start + (int64_t)total_ms * 1000;
    int64_t last_rx = 0;
    int64_t done_at = 0;
    while (esp_timer_get_time() < end) {
        brother_status_t st;
        esp_err_t e = read_one(&st, 250);
        int64_t now = esp_timer_get_time();
        if (e == ESP_OK) {
            last_rx = now;
            log_status("printer reports", &st);
            if (log->count < sizeof(log->items) / sizeof(log->items[0])) {
                log->items[log->count++] = st;
            }
            if (st.status_type == BROTHER_ST_PRINTING_DONE) {
                log->printing_completed = true;
            }
            if (st.status_type == BROTHER_ST_ERROR || brother_status_has_error(&st)) {
                log->error = true;
                break;
            }
            if (st.status_type == BROTHER_ST_PHASE_CHANGE) {
                if (st.phase_type == BROTHER_PHASE_RECEIVING && log->printing_completed) {
                    /* Page done. A multi-page job starts the next page right away, so wait
                     * briefly before treating the whole job as finished. */
                    done_at = now + (int64_t)NEXT_PAGE_WAIT_MS * 1000;
                } else if (st.phase_type == BROTHER_PHASE_PRINTING) {
                    done_at = 0;
                }
            }
        } else if (e != ESP_ERR_TIMEOUT && e != ESP_ERR_INVALID_RESPONSE) {
            break; /* device gone, no IN endpoint ... */
        }
        if (done_at && now > done_at) {
            break;
        }
        if (last_rx && now - last_rx > (int64_t)idle_ms * 1000) {
            break;
        }
        if (!last_rx && now - start > (int64_t)first_ms * 1000) {
            ESP_LOGW(TAG, "printer sent no status within %lu s after the job", (unsigned long)(first_ms / 1000));
            break;
        }
    }
}

/* Reply to a retrieve command (01h 00h value); status messages in between are kept as "last status". */
static int read_setting_reply(void)
{
    for (int i = 0; i < QUERY_REPLY_TRIES; i++) {
        uint8_t buf[64];
        size_t got = 0;
        esp_err_t err = usb_printer_read(buf, sizeof(buf), &got, QUERY_REPLY_TIMEOUT_MS);
        if (err != ESP_OK && err != ESP_ERR_TIMEOUT) {
            return -1;
        }
        uint8_t v;
        if (got && brother_setting_parse_reply(buf, got, &v)) {
            return v;
        }
        brother_status_t st;
        if (got && brother_status_parse(buf, got, &st)) {
            remember(&st);
        }
    }
    return -1;
}

esp_err_t brother_settings_exchange(brother_setting_io_t *io, size_t n)
{
    if (usb_printer_job_begin(0) != ESP_OK) {
        return ESP_ERR_INVALID_STATE; /* never between a job's data and its end */
    }
    usb_printer_summary_t sum;
    usb_printer_get_summary(&sum);
    esp_err_t err = ESP_ERR_INVALID_STATE;
    if (!sum.ready) {
        goto out;
    }
    /* Drop messages the printer sent earlier on its own. */
    brother_status_t st;
    for (int i = 0; i < DRAIN_MAX; i++) {
        if (read_one(&st, DRAIN_TIMEOUT_MS) == ESP_ERR_TIMEOUT) {
            break;
        }
    }
    uint8_t buf[BROTHER_SETTINGS_PREFIX_LEN + 8];  /* also holds the 7/8-byte ESC i X commands */
    size_t len = brother_build_settings_prefix(buf, sizeof(buf));
    err = usb_printer_write(buf, len, QUERY_WRITE_TIMEOUT_MS, NULL);
    for (size_t i = 0; err == ESP_OK && i < n; i++) {
        if (io[i].set_value >= 0) {
            len = brother_build_setting_set(io[i].cmd, (uint8_t)io[i].set_value, buf, sizeof(buf));
            err = usb_printer_write(buf, len, QUERY_WRITE_TIMEOUT_MS, NULL);
            ESP_LOGI(TAG, "setting %c := %d (%s)", io[i].cmd, io[i].set_value, esp_err_to_name(err));
            if (err != ESP_OK) {
                break;
            }
            vTaskDelay(pdMS_TO_TICKS(100)); /* stored in non-volatile memory */
        }
        len = brother_build_setting_get(io[i].cmd, buf, sizeof(buf));
        err = usb_printer_write(buf, len, QUERY_WRITE_TIMEOUT_MS, NULL);
        io[i].value = err == ESP_OK ? read_setting_reply() : -1;
    }
out:
    usb_printer_job_end();
    return err;
}

esp_err_t brother_cancel_job(void)
{
    uint8_t buf[BROTHER_INVALIDATE_LEN + 2];
    memset(buf, BROTHER_CMD_INVALIDATE, BROTHER_INVALIDATE_LEN);
    memcpy(buf + BROTHER_INVALIDATE_LEN, BROTHER_CMD_INITIALIZE, 2);
    esp_err_t err = usb_printer_write(buf, sizeof(buf), QUERY_WRITE_TIMEOUT_MS, NULL);
    ESP_LOGW(TAG, "job aborted on printer (invalidate + initialize): %s", esp_err_to_name(err));
    return err;
}
