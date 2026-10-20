/*
 * Brother TD-2020A Wi-Fi print server - phase 2: USB diagnostic firmware.
 *
 * Starts the USB host (printer detection + descriptor dump), Wi-Fi (setup AP
 * or station), mDNS, the diagnostic web interface and the serial console.
 * No print data is generated or sent by this firmware on its own.
 */
#include <stdio.h>
#include <string.h>

#include "app_config.h"
#include "console_cmds.h"
#include "diag_log.h"
#include "driver/gpio.h"
#include "esp_app_desc.h"
#include "esp_log.h"
#include "esp_ota_ops.h"
#include "esp_system.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "mdns.h"
#include "sdkconfig.h"
#include "usb_printer.h"
#include "web_server.h"
#include "wifi_manager.h"

static const char *TAG = "main";

static void start_mdns(const char *hostname)
{
    esp_err_t err = mdns_init();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "mdns_init: %s", esp_err_to_name(err));
        return;
    }
    mdns_hostname_set(hostname);
    mdns_instance_name_set("Brother TD-2020A Print Server");
    mdns_service_add(NULL, "_http", "_tcp", 80, NULL, 0);
    ESP_LOGI(TAG, "mDNS: http://%s.local/", hostname);
}

#if CONFIG_APP_RESET_BUTTON_GPIO >= 0
static void reset_button_task(void *arg)
{
    const gpio_num_t pin = (gpio_num_t)CONFIG_APP_RESET_BUTTON_GPIO;
    gpio_config_t io = {
        .pin_bit_mask = 1ULL << pin,
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
    };
    gpio_config(&io);
    int held_ms = 0;
    for (;;) {
        vTaskDelay(pdMS_TO_TICKS(100));
        held_ms = gpio_get_level(pin) == 0 ? held_ms + 100 : 0;
        if (held_ms >= 5000) {
            ESP_LOGW(TAG, "reset button held for 5 s: erasing Wi-Fi credentials and restarting");
            wifi_mgr_clear_credentials();
            vTaskDelay(pdMS_TO_TICKS(300));
            esp_restart();
        }
    }
}
#endif

void app_main(void)
{
    /* Mirror the log into RAM first so that early messages appear in the web interface. */
    diag_log_init(CONFIG_APP_LOG_BUFFER_SIZE);

    const esp_app_desc_t *app = esp_app_get_description();
    ESP_LOGI(TAG, "%s %s (ESP-IDF %s) - phase 2 USB diagnostic firmware", app->project_name, app->version,
             app->idf_ver);

    ESP_ERROR_CHECK(cfg_init());

    /* USB first: independent of Wi-Fi, the console report works without network. */
    esp_err_t err = usb_printer_start();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "USB host start failed: %s", esp_err_to_name(err));
    }

    char hostname[33];
    if (cfg_get_str(CFG_NS_SYSTEM, "hostname", hostname, sizeof(hostname)) != ESP_OK || !hostname[0]) {
        strlcpy(hostname, CONFIG_APP_DEFAULT_HOSTNAME, sizeof(hostname));
    }
    ESP_ERROR_CHECK(wifi_mgr_start(hostname));
    start_mdns(hostname);

    err = web_server_start();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "web server start failed: %s", esp_err_to_name(err));
    }

#if CONFIG_APP_RESET_BUTTON_GPIO >= 0
    xTaskCreate(reset_button_task, "reset_btn", 3072, NULL, 2, NULL);
#endif

    /* Reaching this point counts as a successful boot (relevant once OTA is used). */
    esp_ota_mark_app_valid_cancel_rollback();

    console_start();
}
