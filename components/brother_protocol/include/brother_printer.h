/*
 * brother_printer - Brother TD-2000 specific operations on top of usb_printer.
 *
 * Only commands documented in the Brother Raster Command Reference are sent
 * (invalidate, ESC @, ESC i S). Nothing here prints anything.
 */
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "brother_status.h"
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Runs the documented status query (invalidate, initialize, status request)
 * and waits for the 32-byte reply on bulk IN.
 *  - ESP_ERR_INVALID_STATE: no printer, or a job is running (the reference
 *    forbids status requests during printing)
 *  - ESP_ERR_TIMEOUT: no valid reply
 * The reply is also stored as "last status".
 */
esp_err_t brother_query_status(brother_status_t *out);

/*
 * Reads status messages the printer sends on its own (phase changes,
 * "printing completed", errors) for up to `total_ms`. Stops early on an error,
 * 3 s after "printing completed" + "waiting to receive" if no further page
 * starts, after `idle_ms` without any message once one was received, or after
 * `first_ms` if the printer has not sent a single message (it reports
 * "printing" as soon as it starts - a silent printer would otherwise hold the
 * queue for `total_ms` per job).
 * The caller must hold usb_printer_job_begin().
 */
typedef struct {
    brother_status_t items[16];
    size_t count;
    bool printing_completed;    /* status type 01h received */
    bool error;                 /* status type 02h received or error bits set */
} brother_status_log_t;

void brother_collect_statuses(uint32_t total_ms, uint32_t idle_ms, uint32_t first_ms, brother_status_log_t *log);

/*
 * Aborts a partially transmitted job as the reference describes: invalidate
 * bytes followed by initialize (ESC @), which also clears the print buffer.
 * The caller must hold usb_printer_job_begin().
 */
esp_err_t brother_cancel_job(void);

/*
 * Reads and optionally writes printer settings (brother_settings[] in
 * brother_status.h) in one session. For each entry with set_value >= 0 the
 * setting is written first; then every entry is read back into `value`
 * (-1 if the printer did not answer). Leaves the printer in P-touch Template
 * mode; print jobs switch the mode themselves (ESC i a).
 *  - ESP_ERR_INVALID_STATE: no printer, or a job is running
 */
typedef struct {
    char cmd;
    int set_value;      /* -1 = only read */
    int value;          /* out */
} brother_setting_io_t;

esp_err_t brother_settings_exchange(brother_setting_io_t *io, size_t n);

/* Last status received by any of the functions above. Returns false if none yet. */
bool brother_get_last_status(brother_status_t *out, int64_t *age_ms);

#ifdef __cplusplus
}
#endif
