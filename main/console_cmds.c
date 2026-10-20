#include "console_cmds.h"

#include <stdio.h>
#include <string.h>

#include "app_network.h"
#include "diag_log.h"
#include "esp_console.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "usb_printer.h"
#include "wifi_manager.h"

static const char *TAG = "console";

static int cmd_usb(int argc, char **argv)
{
    usb_printer_print_report();
    return 0;
}

static int cmd_usb_refresh(int argc, char **argv)
{
    esp_err_t err = usb_printer_refresh_class_info();
    if (err != ESP_OK) {
        printf("no USB device connected\n");
        return 1;
    }
    usb_printer_print_report();
    return 0;
}

static int cmd_usb_read(int argc, char **argv)
{
    uint8_t buf[512];
    size_t got = 0;
    esp_err_t err = usb_printer_read(buf, sizeof(buf), &got, 1000);
    printf("bulk IN read: %s, %u bytes\n", esp_err_to_name(err), (unsigned)got);
    for (size_t i = 0; i < got; i++) {
        printf("%02x%s", buf[i], (i % 16 == 15) ? "\n" : " ");
    }
    printf("\n");
    return err == ESP_OK || err == ESP_ERR_TIMEOUT ? 0 : 1;
}

static int cmd_status(int argc, char **argv)
{
    usb_printer_summary_t p;
    usb_printer_get_summary(&p);
    wifi_mgr_status_t w;
    wifi_mgr_get_status(&w);
    printf("uptime        : %lld s\n", esp_timer_get_time() / 1000000);
    printf("printer       : %s%s  VID=0x%04x PID=0x%04x  \"%s\" \"%s\"\n",
           p.connected ? "connected" : "not connected", p.ready ? ", ready" : "",
           p.vid, p.pid, p.manufacturer, p.product);
    printf("usb counters  : connects=%lu bytes_written=%lu write_errors=%lu last_error=\"%s\"\n",
           (unsigned long)p.connect_count, (unsigned long)p.bytes_written_total,
           (unsigned long)p.write_errors_total, p.last_error);
    printf("wifi station  : %s ssid=\"%s\" ip=%s rssi=%d last_reason=%u\n",
           w.sta_connected ? "connected" : (w.sta_configured ? "not connected" : "not configured"),
           w.sta_ssid, w.sta_ip[0] ? w.sta_ip : "-", w.rssi, w.last_disconnect_reason);
    printf("setup AP      : %s ssid=\"%s\" clients=%u\n", w.ap_active ? "active" : "off", w.ap_ssid, w.ap_clients);
    printf("hostname      : %s.local\n", w.hostname);
    printf("heap internal : %u free, %u min\n", (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL),
           (unsigned)heap_caps_get_minimum_free_size(MALLOC_CAP_INTERNAL));
    printf("psram         : %u free of %u\n", (unsigned)heap_caps_get_free_size(MALLOC_CAP_SPIRAM),
           (unsigned)heap_caps_get_total_size(MALLOC_CAP_SPIRAM));
    return 0;
}

static int cmd_wifi_set(int argc, char **argv)
{
    if (argc < 2 || argc > 3) {
        printf("usage: wifi_set <ssid> [password]   (quote values containing spaces)\n");
        return 1;
    }
    esp_err_t err = wifi_mgr_set_credentials(argv[1], argc == 3 ? argv[2] : "");
    printf("%s\n", err == ESP_OK ? "stored, connecting..." : esp_err_to_name(err));
    return err == ESP_OK ? 0 : 1;
}

static int cmd_wifi_clear(int argc, char **argv)
{
    wifi_mgr_clear_credentials();
    printf("Wi-Fi credentials erased, restarting into setup mode\n");
    vTaskDelay(pdMS_TO_TICKS(200));
    esp_restart();
    return 0;
}

static int cmd_wifi_start(int argc, char **argv)
{
    esp_err_t err = app_start_network();
    printf("%s\n", err == ESP_OK ? "Wi-Fi started" :
           err == ESP_ERR_INVALID_STATE ? "Wi-Fi is already running" : esp_err_to_name(err));
    return err == ESP_OK ? 0 : 1;
}

static int cmd_ap_info(int argc, char **argv)
{
    wifi_mgr_print_ap_credentials();
    return 0;
}

static int cmd_log(int argc, char **argv)
{
    static char buf[8192];
    diag_log_read(buf, sizeof(buf));
    fputs(buf, stdout);
    return 0;
}

static int cmd_log_level(int argc, char **argv)
{
    static const struct {
        const char *name;
        esp_log_level_t level;
    } levels[] = {
        { "none", ESP_LOG_NONE }, { "error", ESP_LOG_ERROR }, { "warn", ESP_LOG_WARN },
        { "info", ESP_LOG_INFO }, { "debug", ESP_LOG_DEBUG }, { "verbose", ESP_LOG_VERBOSE },
    };
    if (argc != 3) {
        printf("usage: log_level <tag|*> <none|error|warn|info|debug|verbose>\n");
        return 1;
    }
    for (size_t i = 0; i < sizeof(levels) / sizeof(levels[0]); i++) {
        if (strcmp(argv[2], levels[i].name) == 0) {
            esp_log_level_set(argv[1], levels[i].level);
            printf("log level of '%s' set to %s\n", argv[1], levels[i].name);
            return 0;
        }
    }
    printf("unknown level '%s'\n", argv[2]);
    return 1;
}

static int cmd_restart(int argc, char **argv)
{
    esp_restart();
    return 0;
}

void console_start(void)
{
    esp_console_repl_t *repl = NULL;
    esp_console_repl_config_t repl_cfg = ESP_CONSOLE_REPL_CONFIG_DEFAULT();
    repl_cfg.prompt = "printserver>";
    repl_cfg.max_cmdline_length = 256;
    esp_console_dev_uart_config_t uart_cfg = ESP_CONSOLE_DEV_UART_CONFIG_DEFAULT();
    esp_err_t err = esp_console_new_repl_uart(&uart_cfg, &repl_cfg, &repl);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "console init failed: %s", esp_err_to_name(err));
        return;
    }
    esp_console_register_help_command();

    const esp_console_cmd_t cmds[] = {
        { .command = "usb", .help = "Print the full USB descriptor report", .func = cmd_usb },
        { .command = "usb_refresh", .help = "Re-query IEEE 1284 Device ID and port status, then print the report",
          .func = cmd_usb_refresh },
        { .command = "usb_read", .help = "Read once from the bulk IN endpoint (1 s timeout) and hex dump the data",
          .func = cmd_usb_read },
        { .command = "status", .help = "Show printer, Wi-Fi and memory status", .func = cmd_status },
        { .command = "wifi_set", .hint = "<ssid> [password]", .help = "Store Wi-Fi credentials and connect",
          .func = cmd_wifi_set },
        { .command = "wifi_clear", .help = "Erase Wi-Fi credentials and restart into setup AP mode",
          .func = cmd_wifi_clear },
        { .command = "wifi_start", .help = "Start Wi-Fi and web server (after a safe-mode boot)",
          .func = cmd_wifi_start },
        { .command = "ap_info", .help = "Show SSID and password of the setup access point", .func = cmd_ap_info },
        { .command = "log", .help = "Print the buffered log (as shown in the web interface)", .func = cmd_log },
        { .command = "log_level", .hint = "<tag|*> <level>", .help = "Change the log level at runtime",
          .func = cmd_log_level },
        { .command = "restart", .help = "Restart the ESP32", .func = cmd_restart },
    };
    for (size_t i = 0; i < sizeof(cmds) / sizeof(cmds[0]); i++) {
        ESP_ERROR_CHECK(esp_console_cmd_register(&cmds[i]));
    }
    ESP_ERROR_CHECK(esp_console_start_repl(repl));
}
