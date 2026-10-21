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
 * starts, or after `idle_ms` without any message once one was received.
 * The caller must hold usb_printer_job_begin().
 */
typedef struct {
    brother_status_t items[16];
    size_t count;
    bool printing_completed;    /* status type 01h received */
    bool error;                 /* status type 02h received or error bits set */
} brother_status_log_t;

void brother_collect_statuses(uint32_t total_ms, uint32_t idle_ms, brother_status_log_t *log);

/*
 * Aborts a partially transmitted job as the reference describes: invalidate
 * bytes followed by initialize (ESC @), which also clears the print buffer.
 * The caller must hold usb_printer_job_begin().
 */
esp_err_t brother_cancel_job(void);

/* Last status received by any of the functions above. Returns false if none yet. */
bool brother_get_last_status(brother_status_t *out, int64_t *age_ms);

#ifdef __cplusplus
}
#endif
