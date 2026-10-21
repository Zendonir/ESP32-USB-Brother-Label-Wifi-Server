#pragma once

#include <stdbool.h>

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Starts the HTTP server (port 80) with the diagnostic web interface and API. */
esp_err_t web_server_start(void);

/* Enables/disables POST /api/diag/raw at runtime (serial console only; off after every boot). */
void web_server_set_raw_send(bool enable);
bool web_server_raw_send_enabled(void);

#ifdef __cplusplus
}
#endif
