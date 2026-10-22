/*
 * Brother TD-2020A Wi-Fi print server - phase 2: USB diagnostic firmware.
 *
 * Starts the USB host (printer detection + descriptor dump), Wi-Fi (setup AP
 * or station), mDNS, the diagnostic web interface and the serial console.
 * No print data is generated or sent by this firmware on its own.
 */
#include <stdbool.h>
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
#include "led_strip.h"
#include "mdns.h"
#include "print_queue.h"
#include "raw_tcp_server.h"
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
    mdns_txt_item_t txt[] = {
        { "ty", "Brother TD-2020" },
        { "product", "(TD-2020)" },
        { "usb_MFG", "Brother" },
        { "usb_MDL", "TD-2020" },
    };
    mdns_service_add(NULL, "_pdl-datastream", "_tcp", CONFIG_RAW_TCP_PORT, txt, sizeof(txt) / sizeof(txt[0]));
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
    err = raw_tcp_server_start();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "RAW print server start failed: %s", esp_err_to_name(err));
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

#if CONFIG_APP_RESET_BUTTON_GPIO >= 0 || CONFIG_APP_EXT_BUTTON_GPIO >= 0
/*
 * The BOOT button on the board and an optional external button (front panel,
 * wired between its GPIO and GND) do the same - either may be pressed:
 *   short press (released within 2 s)  -> restart (USB host and print queue start afresh)
 *   held for 10 s                      -> erase Wi-Fi credentials and restart into the setup AP
 * Released between 2 and 10 s: nothing - a press that was neither.
 */
#define BUTTON_POLL_MS        50
#define BUTTON_MIN_PRESS_MS   100     /* shorter is contact bounce or noise */
#define BUTTON_SHORT_MAX_MS   2000
#define BUTTON_WIFI_RESET_MS  10000

static const int s_button_pins[] = {
#if CONFIG_APP_RESET_BUTTON_GPIO >= 0
    CONFIG_APP_RESET_BUTTON_GPIO,
#endif
#if CONFIG_APP_EXT_BUTTON_GPIO >= 0 && CONFIG_APP_EXT_BUTTON_GPIO != CONFIG_APP_RESET_BUTTON_GPIO
    CONFIG_APP_EXT_BUTTON_GPIO,
#endif
};
#define BUTTON_COUNT (sizeof(s_button_pins) / sizeof(s_button_pins[0]))

static bool any_button_pressed(void)
{
    for (size_t i = 0; i < BUTTON_COUNT; i++) {
        if (gpio_get_level((gpio_num_t)s_button_pins[i]) == 0) {
            return true;
        }
    }
    return false;
}

static void reset_button_task(void *arg)
{
    uint64_t mask = 0;
    for (size_t i = 0; i < BUTTON_COUNT; i++) {
        mask |= 1ULL << s_button_pins[i];
    }
    /* Internal pull-up: a button to GND needs no resistor. */
    gpio_config_t io = {
        .pin_bit_mask = mask,
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
    };
    gpio_config(&io);
    int held_ms = 0;
    for (;;) {
        vTaskDelay(pdMS_TO_TICKS(BUTTON_POLL_MS));
        if (any_button_pressed()) {
            held_ms += BUTTON_POLL_MS;
            if (held_ms == BUTTON_SHORT_MAX_MS + BUTTON_POLL_MS) {
                ESP_LOGW(TAG, "button held: keep holding for %d s to erase the Wi-Fi settings",
                         BUTTON_WIFI_RESET_MS / 1000);
            }
            if (held_ms >= BUTTON_WIFI_RESET_MS) {
                ESP_LOGW(TAG, "button held for %d s: erasing Wi-Fi credentials and restarting",
                         BUTTON_WIFI_RESET_MS / 1000);
                wifi_mgr_clear_credentials();
                vTaskDelay(pdMS_TO_TICKS(300));
                esp_restart();
            }
            continue;
        }
        if (held_ms >= BUTTON_MIN_PRESS_MS && held_ms <= BUTTON_SHORT_MAX_MS) {
            ESP_LOGW(TAG, "button pressed: restarting");
            vTaskDelay(pdMS_TO_TICKS(300));
            esp_restart();
        } else if (held_ms > BUTTON_SHORT_MAX_MS) {
            ESP_LOGI(TAG, "button released after %d ms: nothing done (short press = restart, "
                     "%d s = Wi-Fi reset)", held_ms, BUTTON_WIFI_RESET_MS / 1000);
        }
        held_ms = 0;
    }
}
#endif

/*
 * The RGB LED (WS2812) on the board is not used. Its data line floats at
 * power-up, so it may light up in some colour and stay that way - send it a
 * single "off" frame. Its colour latches, nothing has to keep running.
 * The red power LED is wired to the supply and cannot be switched off.
 */
static void rgb_led_off(int gpio)
{
    if (gpio < 0) {
        return;
    }
    led_strip_config_t strip = { .strip_gpio_num = gpio, .max_leds = 1 };
    led_strip_rmt_config_t rmt = { .resolution_hz = 10 * 1000 * 1000 };
    led_strip_handle_t led = NULL;
    if (led_strip_new_rmt_device(&strip, &rmt, &led) != ESP_OK) {
        ESP_LOGW(TAG, "RGB LED on GPIO%d: could not be switched off", gpio);
        return;
    }
    led_strip_clear(led);
    led_strip_del(led);
}

void app_main(void)
{
    /* Mirror the log into RAM first so that early messages appear in the web interface. */
    diag_log_init(CONFIG_APP_LOG_BUFFER_SIZE);

    const esp_app_desc_t *app = esp_app_get_description();
    ESP_LOGI(TAG, "%s %s (ESP-IDF %s) - phase 4: RAW port 9100 + print queue", app->project_name, app->version,
             app->idf_ver);

    ESP_ERROR_CHECK(cfg_init());

    rgb_led_off(CONFIG_APP_RGB_LED_GPIO);
#if CONFIG_APP_RGB_LED_GPIO_ALT != CONFIG_APP_RGB_LED_GPIO
    rgb_led_off(CONFIG_APP_RGB_LED_GPIO_ALT);
#endif

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
    ESP_ERROR_CHECK(print_queue_start());

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

#if CONFIG_APP_RESET_BUTTON_GPIO >= 0 || CONFIG_APP_EXT_BUTTON_GPIO >= 0
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
