#include "print_queue.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "brother_printer.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "sdkconfig.h"
#include "usb_printer.h"

static const char *TAG = "queue";

#define MAX_SLOTS               24
#define WRITE_TIMEOUT_MS        30000   /* per 16 KiB chunk; the printer NAKs while its buffer is full */
#define RESULT_WAIT_MS          180000  /* long multi-label jobs */
#define RESULT_IDLE_MS          15000
#define RESULT_FIRST_MS         20000   /* no message at all: do not hold the queue for RESULT_WAIT_MS */
#define WAIT_POLL_MS            3000
#define STATUS_FAILS_BEFORE_SEND 3

typedef struct {
    bool used;
    print_job_info_t info;
    uint32_t seq;           /* FIFO order of queued jobs */
    uint8_t *data;
    bool cancel_requested;
} slot_t;

static slot_t s_slots[MAX_SLOTS];
static SemaphoreHandle_t s_lock;
static SemaphoreHandle_t s_wake;
static uint32_t s_next_id = 1;
static uint32_t s_next_seq = 1;
static uint32_t s_active_id;
static uint32_t s_total_printed;
static uint32_t s_total_failed;

static uint32_t uptime_s(void)
{
    return (uint32_t)(esp_timer_get_time() / 1000000);
}

const char *print_job_state_name(job_state_t st)
{
    switch (st) {
    case JOB_QUEUED:     return "queued";
    case JOB_WAITING:    return "waiting";
    case JOB_PRINTING:   return "printing";
    case JOB_PRINTED:    return "printed";
    case JOB_FAILED:     return "failed";
    case JOB_UNKNOWN:    return "unknown";
    case JOB_CANCELLED:  return "cancelled";
    case JOB_INCOMPLETE: return "incomplete";
    default:             return "?";
    }
}

const char *print_job_source_name(job_source_t src)
{
    return src == JOB_SRC_RAW9100 ? "raw9100" : "http";
}

bool print_job_state_final(job_state_t st)
{
    return st != JOB_QUEUED && st != JOB_WAITING && st != JOB_PRINTING;
}

/* ------------------------------------------------------------------------- */
/* Spool                                                                     */
/* ------------------------------------------------------------------------- */

esp_err_t print_spool_append(print_spool_t *sp, const uint8_t *data, size_t len)
{
    if (sp->len + len > CONFIG_PRINT_QUEUE_MAX_JOB_BYTES) {
        return ESP_ERR_INVALID_SIZE;
    }
    if (sp->len + len > sp->cap) {
        size_t cap = sp->cap ? sp->cap : 16 * 1024;
        while (cap < sp->len + len) {
            cap *= 2;
        }
        if (cap > CONFIG_PRINT_QUEUE_MAX_JOB_BYTES) {
            cap = CONFIG_PRINT_QUEUE_MAX_JOB_BYTES;
        }
        uint8_t *p = heap_caps_realloc(sp->data, cap, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
        if (!p) {
            p = realloc(sp->data, cap);
        }
        if (!p) {
            return ESP_ERR_NO_MEM;
        }
        sp->data = p;
        sp->cap = cap;
    }
    memcpy(sp->data + sp->len, data, len);
    sp->len += len;
    return ESP_OK;
}

void print_spool_free(print_spool_t *sp)
{
    free(sp->data);
    memset(sp, 0, sizeof(*sp));
}

/* ------------------------------------------------------------------------- */
/* Slots (caller holds s_lock)                                               */
/* ------------------------------------------------------------------------- */

static slot_t *find_slot(uint32_t id)
{
    for (int i = 0; i < MAX_SLOTS; i++) {
        if (s_slots[i].used && s_slots[i].info.id == id) {
            return &s_slots[i];
        }
    }
    return NULL;
}

static size_t spool_bytes_locked(void)
{
    size_t n = 0;
    for (int i = 0; i < MAX_SLOTS; i++) {
        if (s_slots[i].used && s_slots[i].data) {
            n += s_slots[i].info.size;
        }
    }
    return n;
}

static void drop_data(slot_t *s)
{
    free(s->data);
    s->data = NULL;
    s->info.data_kept = false;
}

/* Oldest finished job (smallest id) */
static slot_t *oldest_finished(bool with_data_only)
{
    slot_t *best = NULL;
    for (int i = 0; i < MAX_SLOTS; i++) {
        slot_t *s = &s_slots[i];
        if (!s->used || !print_job_state_final(s->info.state) || (with_data_only && !s->data)) {
            continue;
        }
        if (!best || s->info.id < best->info.id) {
            best = s;
        }
    }
    return best;
}

static slot_t *alloc_slot(void)
{
    for (int i = 0; i < MAX_SLOTS; i++) {
        if (!s_slots[i].used) {
            return &s_slots[i];
        }
    }
    slot_t *victim = oldest_finished(false);
    if (victim) {
        drop_data(victim);
        memset(victim, 0, sizeof(*victim));
    }
    return victim;
}

static void finish_locked(slot_t *s, job_state_t st, const char *msg)
{
    s->info.state = st;
    s->info.finished_s = uptime_s();
    strlcpy(s->info.message, msg, sizeof(s->info.message));
    if (st == JOB_PRINTED) {
        s_total_printed++;
        drop_data(s); /* never needed again */
    } else if (st == JOB_FAILED || st == JOB_UNKNOWN) {
        s_total_failed++;
    }
}

/* ------------------------------------------------------------------------- */
/* Public API                                                                */
/* ------------------------------------------------------------------------- */

esp_err_t print_queue_submit(print_spool_t *sp, job_source_t src, const char *client,
                             const char *name, const char *key, uint32_t *id_out)
{
    if (sp->len == 0) {
        print_spool_free(sp);
        return ESP_ERR_INVALID_ARG;
    }
    xSemaphoreTake(s_lock, portMAX_DELAY);
    /* Make room: drop data of the oldest finished jobs (they lose the retry option). */
    while (spool_bytes_locked() + sp->len > CONFIG_PRINT_QUEUE_MEMORY_BUDGET) {
        slot_t *v = oldest_finished(true);
        if (!v) {
            break;
        }
        drop_data(v);
    }
    esp_err_t err = ESP_OK;
    slot_t *s = NULL;
    if (spool_bytes_locked() + sp->len > CONFIG_PRINT_QUEUE_MEMORY_BUDGET) {
        err = ESP_ERR_NO_MEM;
    } else if (!(s = alloc_slot())) {
        err = ESP_ERR_NO_MEM; /* all slots hold active jobs */
    }
    if (err != ESP_OK) {
        xSemaphoreGive(s_lock);
        ESP_LOGE(TAG, "queue full, job from %s (%u bytes) rejected", client, (unsigned)sp->len);
        print_spool_free(sp);
        return err;
    }
    memset(s, 0, sizeof(*s));
    s->used = true;
    s->data = sp->data;
    s->seq = s_next_seq++;
    s->info.id = s_next_id++;
    s->info.state = JOB_QUEUED;
    s->info.source = src;
    s->info.size = sp->len;
    s->info.created_s = uptime_s();
    s->info.data_kept = true;
    strlcpy(s->info.client, client ? client : "", sizeof(s->info.client));
    strlcpy(s->info.name, name ? name : "", sizeof(s->info.name));
    strlcpy(s->info.key, key ? key : "", sizeof(s->info.key));
    strlcpy(s->info.message, "queued", sizeof(s->info.message));
    uint32_t id = s->info.id;
    xSemaphoreGive(s_lock);

    sp->data = NULL;
    sp->len = sp->cap = 0;
    ESP_LOGI(TAG, "job %lu queued: %u bytes from %s (%s)", (unsigned long)id, (unsigned)s->info.size,
             client, print_job_source_name(src));
    if (id_out) {
        *id_out = id;
    }
    xSemaphoreGive(s_wake);
    return ESP_OK;
}

void print_queue_record_incomplete(job_source_t src, const char *client, size_t size, const char *reason)
{
    xSemaphoreTake(s_lock, portMAX_DELAY);
    slot_t *s = alloc_slot();
    if (s) {
        memset(s, 0, sizeof(*s));
        s->used = true;
        s->info.id = s_next_id++;
        s->info.source = src;
        s->info.size = size;
        s->info.created_s = s->info.finished_s = uptime_s();
        s->info.state = JOB_INCOMPLETE;
        strlcpy(s->info.client, client ? client : "", sizeof(s->info.client));
        strlcpy(s->info.message, reason, sizeof(s->info.message));
    }
    xSemaphoreGive(s_lock);
    ESP_LOGW(TAG, "incomplete job from %s discarded (%u bytes): %s", client, (unsigned)size, reason);
}

esp_err_t print_queue_cancel(uint32_t id)
{
    esp_err_t err = ESP_ERR_NOT_FOUND;
    xSemaphoreTake(s_lock, portMAX_DELAY);
    slot_t *s = find_slot(id);
    if (s) {
        if (s->info.state == JOB_QUEUED) {
            finish_locked(s, JOB_CANCELLED, "cancelled");
            err = ESP_OK;
        } else if (s->info.state == JOB_WAITING) {
            s->cancel_requested = true; /* the worker finishes it */
            err = ESP_OK;
        } else {
            err = ESP_ERR_INVALID_STATE;
        }
    }
    xSemaphoreGive(s_lock);
    if (err == ESP_OK) {
        ESP_LOGI(TAG, "job %lu cancelled", (unsigned long)id);
    }
    return err;
}

esp_err_t print_queue_retry(uint32_t id)
{
    esp_err_t err = ESP_ERR_NOT_FOUND;
    xSemaphoreTake(s_lock, portMAX_DELAY);
    slot_t *s = find_slot(id);
    if (s) {
        job_state_t st = s->info.state;
        if ((st == JOB_FAILED || st == JOB_UNKNOWN || st == JOB_CANCELLED) && s->data) {
            s->info.state = JOB_QUEUED;
            s->seq = s_next_seq++;
            s->cancel_requested = false;
            s->info.printer_completed = s->info.printer_error = false;
            s->info.started_s = s->info.finished_s = 0;
            strlcpy(s->info.message, "queued again", sizeof(s->info.message));
            err = ESP_OK;
        } else {
            err = ESP_ERR_INVALID_STATE;
        }
    }
    xSemaphoreGive(s_lock);
    if (err == ESP_OK) {
        ESP_LOGI(TAG, "job %lu queued again", (unsigned long)id);
        xSemaphoreGive(s_wake);
    }
    return err;
}

esp_err_t print_queue_find_key(const char *key, uint32_t *id_out)
{
    if (!key || !key[0]) {
        return ESP_ERR_NOT_FOUND;
    }
    esp_err_t err = ESP_ERR_NOT_FOUND;
    xSemaphoreTake(s_lock, portMAX_DELAY);
    for (int i = 0; i < MAX_SLOTS; i++) {
        const slot_t *s = &s_slots[i];
        if (s->used && s->info.state != JOB_INCOMPLETE && strcmp(s->info.key, key) == 0) {
            *id_out = s->info.id;
            err = ESP_OK;
            break;
        }
    }
    xSemaphoreGive(s_lock);
    return err;
}

esp_err_t print_queue_get(uint32_t id, print_job_info_t *out)
{
    xSemaphoreTake(s_lock, portMAX_DELAY);
    slot_t *s = find_slot(id);
    if (s) {
        *out = s->info;
    }
    xSemaphoreGive(s_lock);
    return s ? ESP_OK : ESP_ERR_NOT_FOUND;
}

void print_queue_get_stats(print_queue_stats_t *out)
{
    memset(out, 0, sizeof(*out));
    xSemaphoreTake(s_lock, portMAX_DELAY);
    for (int i = 0; i < MAX_SLOTS; i++) {
        if (s_slots[i].used && (s_slots[i].info.state == JOB_QUEUED || s_slots[i].info.state == JOB_WAITING)) {
            out->queued++;
        }
    }
    out->active_id = s_active_id;
    out->total_printed = s_total_printed;
    out->total_failed = s_total_failed;
    out->spool_bytes = spool_bytes_locked();
    xSemaphoreGive(s_lock);
}

esp_err_t print_queue_wait(uint32_t id, uint32_t timeout_ms, print_job_info_t *out)
{
    int64_t end = esp_timer_get_time() + (int64_t)timeout_ms * 1000;
    for (;;) {
        esp_err_t err = print_queue_get(id, out);
        if (err != ESP_OK || print_job_state_final(out->state)) {
            return err;
        }
        if (esp_timer_get_time() > end) {
            return ESP_ERR_TIMEOUT;
        }
        vTaskDelay(pdMS_TO_TICKS(200));
    }
}

/* ------------------------------------------------------------------------- */
/* JSON / console                                                            */
/* ------------------------------------------------------------------------- */

void print_queue_job_json(const print_job_info_t *j, strbuf_t *sb)
{
    bool can_retry = (j->state == JOB_FAILED || j->state == JOB_UNKNOWN || j->state == JOB_CANCELLED) && j->data_kept;
    bool can_cancel = j->state == JOB_QUEUED || j->state == JOB_WAITING;
    sb_printf(sb, "{\"id\":%lu,\"state\":\"%s\",\"source\":\"%s\",\"client\":", (unsigned long)j->id,
              print_job_state_name(j->state), print_job_source_name(j->source));
    sb_json_str(sb, j->client);
    sb_printf(sb, ",\"size\":%u,\"created_s\":%lu,\"started_s\":%lu,\"finished_s\":%lu,\"attempts\":%u,"
              "\"printer_completed\":%s,\"printer_error\":%s,\"can_retry\":%s,\"can_cancel\":%s,\"message\":",
              (unsigned)j->size, (unsigned long)j->created_s, (unsigned long)j->started_s,
              (unsigned long)j->finished_s, j->attempts, j->printer_completed ? "true" : "false",
              j->printer_error ? "true" : "false", can_retry ? "true" : "false", can_cancel ? "true" : "false");
    sb_json_str(sb, j->message);
    sb_puts(sb, ",\"name\":");
    sb_json_str(sb, j->name);
    sb_puts(sb, ",\"key\":");
    sb_json_str(sb, j->key);
    sb_putc(sb, '}');
}

void print_queue_list_json(strbuf_t *sb)
{
    print_queue_stats_t st;
    print_queue_get_stats(&st);
    sb_printf(sb, "{\"stats\":{\"queued\":%lu,\"active_id\":%lu,\"total_printed\":%lu,\"total_failed\":%lu,"
              "\"spool_bytes\":%u,\"uptime_s\":%lu},\"jobs\":[",
              (unsigned long)st.queued, (unsigned long)st.active_id, (unsigned long)st.total_printed,
              (unsigned long)st.total_failed, (unsigned)st.spool_bytes, (unsigned long)uptime_s());
    /* newest first: collect ids */
    print_job_info_t list[MAX_SLOTS];
    int n = 0;
    xSemaphoreTake(s_lock, portMAX_DELAY);
    for (int i = 0; i < MAX_SLOTS; i++) {
        if (s_slots[i].used) {
            list[n++] = s_slots[i].info;
        }
    }
    xSemaphoreGive(s_lock);
    for (int i = 0; i < n; i++) {
        for (int k = i + 1; k < n; k++) {
            if (list[k].id > list[i].id) {
                print_job_info_t t = list[i];
                list[i] = list[k];
                list[k] = t;
            }
        }
    }
    for (int i = 0; i < n; i++) {
        if (i) {
            sb_putc(sb, ',');
        }
        print_queue_job_json(&list[i], sb);
    }
    sb_puts(sb, "]}");
}

void print_queue_print_list(void)
{
    print_job_info_t list[MAX_SLOTS];
    int n = 0;
    xSemaphoreTake(s_lock, portMAX_DELAY);
    for (int i = 0; i < MAX_SLOTS; i++) {
        if (s_slots[i].used) {
            list[n++] = s_slots[i].info;
        }
    }
    xSemaphoreGive(s_lock);
    printf("  ID  STATE       SOURCE   CLIENT           BYTES  AGE  MESSAGE\n");
    uint32_t now = uptime_s();
    for (int i = 0; i < n; i++) {
        const print_job_info_t *j = &list[i];
        printf("%4lu  %-10s  %-7s  %-15s %6u %4lus  %s%s%s\n", (unsigned long)j->id, print_job_state_name(j->state),
               print_job_source_name(j->source), j->client, (unsigned)j->size,
               (unsigned long)(now - j->created_s), j->name, j->name[0] ? ": " : "", j->message);
    }
    if (n == 0) {
        printf("  (no jobs)\n");
    }
}

/* ------------------------------------------------------------------------- */
/* Worker                                                                    */
/* ------------------------------------------------------------------------- */

static slot_t *next_queued_locked(void)
{
    slot_t *best = NULL;
    for (int i = 0; i < MAX_SLOTS; i++) {
        slot_t *s = &s_slots[i];
        if (s->used && s->info.state == JOB_QUEUED && (!best || s->seq < best->seq)) {
            best = s;
        }
    }
    return best;
}

static void set_state(uint32_t id, job_state_t st, const char *msg)
{
    xSemaphoreTake(s_lock, portMAX_DELAY);
    slot_t *s = find_slot(id);
    if (s) {
        s->info.state = st;
        if (msg) {
            strlcpy(s->info.message, msg, sizeof(s->info.message));
        }
    }
    xSemaphoreGive(s_lock);
}

static void finish(uint32_t id, job_state_t st, const char *msg, bool completed, bool error)
{
    xSemaphoreTake(s_lock, portMAX_DELAY);
    slot_t *s = find_slot(id);
    if (s) {
        s->info.printer_completed = completed;
        s->info.printer_error = error;
        finish_locked(s, st, msg);
    }
    s_active_id = 0;
    xSemaphoreGive(s_lock);
    ESP_LOGI(TAG, "job %lu %s: %s", (unsigned long)id, print_job_state_name(st), msg);
}

/* 0 = go on, 1 = cancel was requested while waiting, 2 = job already ended (e.g. cancelled while queued) */
static int cancel_state(uint32_t id)
{
    xSemaphoreTake(s_lock, portMAX_DELAY);
    slot_t *s = find_slot(id);
    int c = (!s || print_job_state_final(s->info.state)) ? 2 : s->cancel_requested ? 1 : 0;
    xSemaphoreGive(s_lock);
    return c;
}

/* Waits until the printer is connected and reports no error. Returns false if the job ended. */
static bool wait_for_printer(uint32_t id)
{
    int64_t start = esp_timer_get_time();
    int status_fails = 0;
    char msg[96];
    for (;;) {
        int c = cancel_state(id);
        if (c == 1) {
            finish(id, JOB_CANCELLED, "cancelled while waiting for the printer", false, false);
        }
        if (c) {
            xSemaphoreTake(s_lock, portMAX_DELAY);
            s_active_id = 0;
            xSemaphoreGive(s_lock);
            return false;
        }
        usb_printer_summary_t sum;
        usb_printer_get_summary(&sum);
        if (!sum.ready) {
            strlcpy(msg, "waiting: printer not connected", sizeof(msg));
        } else {
            brother_status_t st;
            esp_err_t err = brother_query_status(&st);
            if (err == ESP_OK && !brother_status_has_error(&st)) {
                return true;
            }
            if (err == ESP_OK) {
                strbuf_t sb;
                sb_init(&sb, msg, sizeof(msg));
                sb_puts(&sb, "waiting: ");
                brother_status_errors_text(&st, &sb);
            } else if (++status_fails >= STATUS_FAILS_BEFORE_SEND) {
                /* Printer is connected but does not answer status requests: send anyway,
                 * its messages after the job decide the result. */
                ESP_LOGW(TAG, "no status reply, sending job %lu anyway", (unsigned long)id);
                return true;
            } else {
                strlcpy(msg, "waiting: no status reply", sizeof(msg));
            }
        }
        set_state(id, JOB_WAITING, msg);
        if (esp_timer_get_time() - start > (int64_t)CONFIG_PRINT_QUEUE_WAIT_PRINTER_S * 1000000) {
            finish(id, JOB_FAILED, msg + 9 /* without "waiting: " */, false, true);
            return false;
        }
        vTaskDelay(pdMS_TO_TICKS(WAIT_POLL_MS));
    }
}

static void print_one(uint32_t id)
{
    if (!wait_for_printer(id)) {
        return;
    }
    uint8_t *data;
    size_t size;
    xSemaphoreTake(s_lock, portMAX_DELAY);
    slot_t *s = find_slot(id);
    if (s && print_job_state_final(s->info.state)) {
        s_active_id = 0; /* cancelled in the meantime */
        xSemaphoreGive(s_lock);
        return;
    }
    if (!s || !s->data) {
        xSemaphoreGive(s_lock);
        finish(id, JOB_FAILED, "job data no longer available", false, false);
        return;
    }
    /* The worker is the only one that frees data of a job in PRINTING state. */
    s->info.state = JOB_PRINTING;
    s->info.started_s = uptime_s();
    s->info.attempts++;
    strlcpy(s->info.message, "sending to printer", sizeof(s->info.message));
    data = s->data;
    size = s->info.size;
    xSemaphoreGive(s_lock);

    if (usb_printer_job_begin(10000) != ESP_OK) {
        finish(id, JOB_FAILED, "printer busy (another job did not finish)", false, false);
        return;
    }
    size_t written = 0;
    esp_err_t err = usb_printer_write(data, size, WRITE_TIMEOUT_MS, &written);
    brother_status_log_t log;
    memset(&log, 0, sizeof(log));
    if (err == ESP_OK) {
        set_state(id, JOB_PRINTING, "sent, waiting for printer confirmation");
        brother_collect_statuses(RESULT_WAIT_MS, RESULT_IDLE_MS, RESULT_FIRST_MS, &log);
    } else if (err != ESP_ERR_NOT_FOUND) {
        brother_cancel_job();
    }
    usb_printer_job_end();

    char msg[96];
    if (err != ESP_OK) {
        snprintf(msg, sizeof(msg), "USB transfer failed after %u of %u bytes (%s)", (unsigned)written,
                 (unsigned)size, esp_err_to_name(err));
        finish(id, JOB_FAILED, msg, false, false);
    } else if (log.error) {
        strbuf_t sb;
        sb_init(&sb, msg, sizeof(msg));
        sb_puts(&sb, "printer error: ");
        for (size_t i = log.count; i-- > 0;) {
            if (brother_status_has_error(&log.items[i])) {
                brother_status_errors_text(&log.items[i], &sb);
                break;
            }
        }
        finish(id, JOB_FAILED, msg, false, true);
    } else if (log.printing_completed) {
        finish(id, JOB_PRINTED, "printer reported printing completed", true, false);
    } else {
        finish(id, JOB_UNKNOWN, "sent, but the printer did not confirm the print", false, false);
    }
}

static void worker_task(void *arg)
{
    for (;;) {
        xSemaphoreTake(s_lock, portMAX_DELAY);
        slot_t *s = next_queued_locked();
        uint32_t id = 0;
        if (s) {
            id = s->info.id;
            s_active_id = id;
        }
        xSemaphoreGive(s_lock);
        if (!id) {
            xSemaphoreTake(s_wake, portMAX_DELAY);
            continue;
        }
        print_one(id);
    }
}

esp_err_t print_queue_start(void)
{
    s_lock = xSemaphoreCreateMutex();
    s_wake = xSemaphoreCreateBinary();
    if (!s_lock || !s_wake) {
        return ESP_ERR_NO_MEM;
    }
    if (xTaskCreate(worker_task, "print_queue", 6144, NULL, 5, NULL) != pdPASS) {
        return ESP_ERR_NO_MEM;
    }
    ESP_LOGI(TAG, "print queue started (max job %d KiB, memory %d KiB)",
             CONFIG_PRINT_QUEUE_MAX_JOB_BYTES / 1024, CONFIG_PRINT_QUEUE_MEMORY_BUDGET / 1024);
    return ESP_OK;
}
