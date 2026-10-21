/*
 * print_queue - central FIFO for all print jobs (port 9100, HTTP).
 *
 *  - jobs are spooled completely (PSRAM) before they are sent, so an
 *    interrupted network transfer never reaches the printer
 *  - exactly one job is sent to the printer at a time, never interleaved
 *  - before sending, the Brother status is checked; while the printer reports
 *    an error (cover open, no media) or is not connected the job waits
 *  - after sending, the printer's own messages decide the result:
 *      printed  - printer reported "printing completed"
 *      failed   - printer reported an error (it discards the job) or USB error
 *      unknown  - data was accepted but the printer did not confirm
 *  - failed/unknown jobs are never repeated automatically (no duplicate labels);
 *    they can be retried explicitly while their data is still kept
 */
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"
#include "strbuf.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    JOB_QUEUED,
    JOB_WAITING,        /* waiting for the printer (not connected / error) */
    JOB_PRINTING,
    JOB_PRINTED,
    JOB_FAILED,
    JOB_UNKNOWN,        /* sent, but no confirmation from the printer */
    JOB_CANCELLED,
    JOB_INCOMPLETE,     /* network transfer did not complete, nothing was sent */
} job_state_t;

typedef enum {
    JOB_SRC_RAW9100,
    JOB_SRC_HTTP,
} job_source_t;

typedef struct {
    uint32_t id;
    job_state_t state;
    job_source_t source;
    char client[16];
    size_t size;
    uint32_t created_s;     /* uptime seconds */
    uint32_t started_s;
    uint32_t finished_s;
    bool printer_completed;
    bool printer_error;
    bool data_kept;         /* retry possible */
    uint8_t attempts;
    char message[96];
} print_job_info_t;

typedef struct {
    uint32_t queued;        /* queued + waiting */
    uint32_t active_id;     /* job being printed, 0 = none */
    uint32_t total_printed;
    uint32_t total_failed;
    size_t spool_bytes;     /* memory held by job data */
} print_queue_stats_t;

/* Growable spool buffer in PSRAM for receiving a job. */
typedef struct {
    uint8_t *data;
    size_t len;
    size_t cap;
} print_spool_t;

esp_err_t print_queue_start(void);

/* Appends to a spool; ESP_ERR_INVALID_SIZE if the job exceeds the configured maximum. */
esp_err_t print_spool_append(print_spool_t *sp, const uint8_t *data, size_t len);
void print_spool_free(print_spool_t *sp);

/* Takes ownership of the spool data (also on error). */
esp_err_t print_queue_submit(print_spool_t *sp, job_source_t src, const char *client, uint32_t *id_out);

/* Records a job whose network transfer failed (nothing is printed), for the job list. */
void print_queue_record_incomplete(job_source_t src, const char *client, size_t size, const char *reason);

esp_err_t print_queue_cancel(uint32_t id);   /* queued/waiting jobs only */
esp_err_t print_queue_retry(uint32_t id);    /* failed/unknown/cancelled jobs with kept data */
esp_err_t print_queue_get(uint32_t id, print_job_info_t *out);
void print_queue_get_stats(print_queue_stats_t *out);

/* Blocks until the job reached a final state or the timeout expired. */
esp_err_t print_queue_wait(uint32_t id, uint32_t timeout_ms, print_job_info_t *out);

const char *print_job_state_name(job_state_t st);
const char *print_job_source_name(job_source_t src);
bool print_job_state_final(job_state_t st);

void print_queue_job_json(const print_job_info_t *j, strbuf_t *sb);
void print_queue_list_json(strbuf_t *sb);   /* {"stats":{...},"jobs":[...]} newest first */
void print_queue_print_list(void);          /* console */

#ifdef __cplusplus
}
#endif
