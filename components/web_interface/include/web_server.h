#pragma once

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Starts the HTTP server (port 80) with the diagnostic web interface and API. */
esp_err_t web_server_start(void);

#ifdef __cplusplus
}
#endif
