/*
 * usb_printer - USB host transport for a single USB printer.
 *
 * Responsibilities:
 *   - run the ESP-IDF USB Host Library (daemon + client task)
 *   - enumerate attached devices and record ALL descriptors (diagnostic mode)
 *   - select the printer interface (USB Printer Class 0x07/0x01, nothing device specific)
 *   - USB Printer Class requests GET_DEVICE_ID / GET_PORT_STATUS (USB Printer Class 1.1)
 *   - bulk OUT writes / bulk IN reads with timeouts and endpoint recovery
 *   - detect disconnects and accept the next device automatically
 *
 * Nothing in here sends Brother specific commands. Data passed to
 * usb_printer_write() is forwarded unchanged.
 */
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"
#include "strbuf.h"
#include "usb_desc_report.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    bool connected;         /* a device is enumerated and opened */
    bool ready;             /* printer interface claimed, bulk OUT available */
    bool is_printer_class;
    uint16_t vid;
    uint16_t pid;
    char manufacturer[64];
    char product[64];
    uint32_t connect_count;
    uint32_t bytes_written_total;
    uint32_t write_errors_total;
    char last_error[96];
} usb_printer_summary_t;

/* Starts the USB host stack and the printer manager. Call once. */
esp_err_t usb_printer_start(void);

void usb_printer_get_summary(usb_printer_summary_t *out);

/* Formats the full descriptor report (thread safe; takes an internal lock). */
void usb_printer_report_text(strbuf_t *sb);
void usb_printer_report_json(strbuf_t *sb);

/* Prints the full text report to stdout (UART console). */
void usb_printer_print_report(void);

/* Re-runs GET_DEVICE_ID and GET_PORT_STATUS (read-only class requests). */
esp_err_t usb_printer_refresh_class_info(void);

/* USB Printer Class GET_PORT_STATUS; *status receives the raw status byte. */
esp_err_t usb_printer_get_port_status(uint8_t *status);

/*
 * Writes data unchanged to the bulk OUT endpoint.
 *  - blocks until all bytes are acknowledged, the device disappears, or no
 *    progress was made for `timeout_ms` (per chunk)
 *  - *written (optional) receives the number of bytes the device acknowledged
 * Returns ESP_OK, ESP_ERR_INVALID_STATE (no printer), ESP_ERR_TIMEOUT,
 * ESP_ERR_NOT_FOUND (device gone during transfer) or ESP_FAIL.
 * A successful return means "USB transfer acknowledged", NOT "label printed".
 */
esp_err_t usb_printer_write(const uint8_t *data, size_t len, uint32_t timeout_ms, size_t *written);

/* Reads from the bulk IN endpoint if present. *got receives the byte count. */
esp_err_t usb_printer_read(uint8_t *buf, size_t len, size_t *got, uint32_t timeout_ms);

/*
 * Exclusive access for a complete print job or command sequence (e.g. status
 * request + reply). Everything that talks to the printer through more than one
 * usb_printer_write()/read() call must hold this so that sequences are never
 * interleaved. Returns ESP_ERR_TIMEOUT if another job is still running.
 */
esp_err_t usb_printer_job_begin(uint32_t timeout_ms);
void usb_printer_job_end(void);

#ifdef __cplusplus
}
#endif
