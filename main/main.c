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
#include "app_network.h"
#include "console_cmds.h"
#include "diag_log.h"
#include "driver/gpio.h"
#include "esp_app_desc.h"
#include "esp_log.h"
#include "esp_attr.h"
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

/*
 * Brownout tracking. The RF calibration at Wi-Fi start draws current peaks of
 * several hundred mA; with a weak USB supply the chip resets right there and
 * would loop forever. After repeated brownout resets the firmware starts
 * without Wi-Fi so that the USB diagnostics on the serial console still work.
 * RTC_NOINIT memory survives software and brownout resets but not power-on.
 */
#define BROWNOUT_MAGIC          0xB0D0C0DEu
#define BROWNOUT_SAFE_MODE_AT   2
static RTC_NOINIT_ATTR uint32_t s_brownout_magic;
static RTC_NOINIT_ATTR uint32_t s_brownout_count;

static bool s_network_started;
static char s_hostname[33];

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

esp_err_t app_start_network(void)
{
    if (s_network_started) {
        return ESP_ERR_INVALID_STATE;
    }
    esp_err_t err = wifi_mgr_start(s_hostname);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Wi-Fi start failed: %s", esp_err_to_name(err));
        return err;
    }
    s_network_started = true;
    start_mdns(s_hostname);
    err = web_server_start();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "web server start failed: %s", esp_err_to_name(err));
    }
    return ESP_OK;
}

static bool update_brownout_counter(void)
{
    if (s_brownout_magic != BROWNOUT_MAGIC) {
        s_brownout_magic = BROWNOUT_MAGIC;
        s_brownout_count = 0;
    }
    if (esp_reset_reason() == ESP_RST_BROWNOUT) {
        s_brownout_count++;
        ESP_LOGW(TAG, "last reset was a BROWNOUT (supply voltage dropped), %lu in a row",
                 (unsigned long)s_brownout_count);
    } else {
        s_brownout_count = 0;
    }
    return s_brownout_count >= BROWNOUT_SAFE_MODE_AT;
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

#if CONFIG_APP_USB_DEBUG_LOG
    /* ESP-IDF logs a detected connection and the enumeration stages only at debug level. */
    esp_log_level_set("HUB", ESP_LOG_DEBUG);
    esp_log_level_set("ENUM", ESP_LOG_DEBUG);
#endif

    /* USB first: independent of Wi-Fi, the console report works without network. */
    esp_err_t err = usb_printer_start();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "USB host start failed: %s", esp_err_to_name(err));
    }

    if (cfg_get_str(CFG_NS_SYSTEM, "hostname", s_hostname, sizeof(s_hostname)) != ESP_OK || !s_hostname[0]) {
        strlcpy(s_hostname, CONFIG_APP_DEFAULT_HOSTNAME, sizeof(s_hostname));
    }

    bool safe_mode = update_brownout_counter();
    if (safe_mode) {
        ESP_LOGE(TAG, "SAFE MODE: %lu brownout resets in a row - Wi-Fi NOT started.",
                 (unsigned long)s_brownout_count);
        ESP_LOGE(TAG, "The power supply cannot deliver the Wi-Fi current peaks. Use another USB port/cable "
                      "or a 5 V supply >= 1 A. USB diagnostics work on this console ('usb'); "
                      "'wifi_start' tries Wi-Fi anyway.");
        /* Next reset gets a fresh chance (unless it is a brownout again). */
        s_brownout_count = BROWNOUT_SAFE_MODE_AT - 1;
    } else {
        ESP_ERROR_CHECK(app_start_network());
    }

#if CONFIG_APP_RESET_BUTTON_GPIO >= 0
    xTaskCreate(reset_button_task, "reset_btn", 3072, NULL, 2, NULL);
#endif

    /* Reaching this point counts as a successful boot (relevant once OTA is used). */
    esp_ota_mark_app_valid_cancel_rollback();

    console_start();

    if (!safe_mode) {
        /* Wi-Fi survived its start-up: forget earlier brownouts. */
        vTaskDelay(pdMS_TO_TICKS(10000));
        s_brownout_count = 0;
    }
}
