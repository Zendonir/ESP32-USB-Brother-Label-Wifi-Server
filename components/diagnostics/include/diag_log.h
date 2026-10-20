/*
 * diag_log - keeps a copy of the ESP-IDF log output in a RAM ring buffer so
 * it can be shown in the web interface. UART output is unchanged.
 *
 * Never log secrets (passwords, API keys): everything logged ends up here.
 */
#pragma once

#include <stddef.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

esp_err_t diag_log_init(size_t ring_size);

/* Copies the most recent log text (starting at a line boundary) into dst. Returns bytes copied. */
size_t diag_log_read(char *dst, size_t dst_size);

void diag_log_clear(void);

#ifdef __cplusplus
}
#endif
