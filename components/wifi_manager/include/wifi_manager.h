/*
 * wifi_manager - station mode with automatic reconnect plus a password
 * protected setup access point.
 *
 *  - no credentials stored        -> setup AP only
 *  - credentials stored           -> station; if not connected within
 *                                    CONFIG_WIFI_MGR_FALLBACK_AP_DELAY_S the setup
 *                                    AP is started additionally (recovery)
 *  - station got an IP            -> a running setup AP is stopped after 60 s
 *
 * The setup AP password is random per device (generated on first boot, stored
 * in NVS) and is only shown on the serial console.
 */
#pragma once

#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    bool sta_configured;
    bool sta_connected;
    bool ap_active;
    char sta_ssid[33];
    char sta_ip[16];
    char sta_gw[16];
    int8_t rssi;
    uint8_t last_disconnect_reason;
    char ap_ssid[33];
    char ap_ip[16];
    uint8_t ap_clients;
    char hostname[33];
} wifi_mgr_status_t;

/* Returns ESP_ERR_INVALID_STATE if already started. */
esp_err_t wifi_mgr_start(const char *hostname);
bool wifi_mgr_is_started(void);
void wifi_mgr_get_status(wifi_mgr_status_t *out);

/* Stores new station credentials and (re)connects (only stores them while Wi-Fi is not started). Password may be empty (open network). */
esp_err_t wifi_mgr_set_credentials(const char *ssid, const char *password);
esp_err_t wifi_mgr_clear_credentials(void);

/* For the serial console only - never send this over the network or into the log. */
void wifi_mgr_print_ap_credentials(void);

/* True if `ipv4` (network byte order, as from lwIP) is the local address of the setup AP. */
bool wifi_mgr_is_ap_local_ip(uint32_t ipv4);

#ifdef __cplusplus
}
#endif
