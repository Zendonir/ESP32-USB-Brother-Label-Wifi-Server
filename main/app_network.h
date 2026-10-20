#pragma once

#include "esp_err.h"

/* Starts Wi-Fi, mDNS and the web server (once). Used at boot and by the console in safe mode. */
esp_err_t app_start_network(void);
