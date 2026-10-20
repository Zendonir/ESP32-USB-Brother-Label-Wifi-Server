/*
 * app_config - thin, thread-safe wrapper around NVS for persistent settings.
 *
 * Values are stored in the default "nvs" partition. Secrets (Wi-Fi password,
 * later API key) are stored here too; never log their values.
 * Note: NVS encryption is not enabled in this phase (see docs/known-limitations).
 */
#pragma once

#include <stddef.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

#define CFG_NS_WIFI   "wifi"
#define CFG_NS_SYSTEM "system"

esp_err_t cfg_init(void);
esp_err_t cfg_get_str(const char *ns, const char *key, char *buf, size_t size);
esp_err_t cfg_set_str(const char *ns, const char *key, const char *value);
esp_err_t cfg_erase_key(const char *ns, const char *key);
esp_err_t cfg_erase_namespace(const char *ns);

#ifdef __cplusplus
}
#endif
