/*
 * USB descriptor parsing and report formatting.
 *
 * Pure C (no ESP-IDF dependencies) so it can be unit-tested on the host
 * (see tests/host). Works on raw descriptor bytes exactly as received from
 * the device, nothing about the Brother TD-2020A is assumed here.
 */
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "strbuf.h"

#ifdef __cplusplus
extern "C" {
#endif

#define USB_REPORT_MAX_CFG_LEN      1024
#define USB_REPORT_MAX_STR_LEN      128
#define USB_REPORT_MAX_EXTRA_STR    8
#define USB_REPORT_DEVID_MAX        1024

/* Standard USB descriptor types (USB 2.0 spec, table 9-5) */
#define USB_DT_DEVICE               0x01
#define USB_DT_CONFIGURATION        0x02
#define USB_DT_STRING               0x03
#define USB_DT_INTERFACE            0x04
#define USB_DT_ENDPOINT             0x05
#define USB_DT_DEVICE_QUALIFIER     0x06
#define USB_DT_OTHER_SPEED_CONFIG   0x07
#define USB_DT_INTERFACE_POWER      0x08
#define USB_DT_OTG                  0x09
#define USB_DT_DEBUG                0x0a
#define USB_DT_IAD                  0x0b

/* Interface classes relevant for the analysis (usb.org class codes) */
#define USB_IF_CLASS_PRINTER        0x07
#define USB_IF_CLASS_MASS_STORAGE   0x08
#define USB_IF_CLASS_VENDOR         0xff
#define USB_PRINTER_SUBCLASS        0x01
/* USB Printer Class 1.1, section 4.2 / 4.3 */
#define USB_PRINTER_PROTO_UNIDIR    0x01
#define USB_PRINTER_PROTO_BIDIR     0x02
#define USB_PRINTER_PROTO_1284_4    0x03

typedef enum {
    USB_REPORT_SPEED_UNKNOWN = -1,
    USB_REPORT_SPEED_LOW = 0,
    USB_REPORT_SPEED_FULL = 1,
    USB_REPORT_SPEED_HIGH = 2,
} usb_report_speed_t;

typedef struct {
    uint8_t index;
    char text[64];
} usb_report_string_t;

/* Interface chosen for printing (or the best candidate found). */
typedef struct {
    bool found;
    bool is_printer_class;      /* bInterfaceClass == 0x07 && subclass == 0x01 */
    uint8_t interface_number;
    uint8_t alternate_setting;
    uint8_t interface_class;
    uint8_t interface_subclass;
    uint8_t interface_protocol;
    uint8_t ep_out;             /* bulk OUT address, 0 = none */
    uint16_t ep_out_mps;
    uint8_t ep_in;              /* bulk IN address, 0 = none */
    uint16_t ep_in_mps;
} usb_printer_if_info_t;

typedef struct {
    bool valid;                 /* a device is (or was) attached and the descriptors below are filled */
    bool connected;             /* currently attached */
    uint32_t connect_count;
    uint8_t dev_addr;
    int speed;                  /* usb_report_speed_t */
    uint8_t active_config_value;

    uint8_t dev_desc[18];
    uint8_t cfg_desc[USB_REPORT_MAX_CFG_LEN];
    uint16_t cfg_len;           /* bytes stored in cfg_desc */
    uint16_t cfg_total_len;     /* wTotalLength reported by the device */

    uint16_t lang_id;           /* first LANGID from string descriptor 0, 0 = unknown */
    char manufacturer[USB_REPORT_MAX_STR_LEN];
    char product[USB_REPORT_MAX_STR_LEN];
    char serial[USB_REPORT_MAX_STR_LEN];
    usb_report_string_t extra_strings[USB_REPORT_MAX_EXTRA_STR]; /* iConfiguration / iInterface */
    uint8_t n_extra_strings;

    usb_printer_if_info_t pif;
    bool claimed;
    char claim_err[40];

    /* USB Printer Class GET_DEVICE_ID (IEEE 1284 Device ID) */
    bool devid_attempted;
    char devid_err[40];         /* empty = success */
    uint16_t devid_len_field;   /* the 2-byte length prefix as interpreted */
    bool devid_len_little_endian;
    char devid[USB_REPORT_DEVID_MAX];

    /* USB Printer Class GET_PORT_STATUS */
    bool port_status_attempted;
    char port_status_err[40];
    uint8_t port_status;
} usb_dev_report_t;

static inline uint16_t usb_rd16(const uint8_t *p)
{
    return (uint16_t)(p[0] | (p[1] << 8));
}

const char *usb_class_name(uint8_t cls);
const char *usb_desc_type_name(uint8_t type);
const char *usb_speed_name(int speed);

/*
 * Walks a configuration descriptor and picks the interface to use for print data:
 *   1. Printer class (07/01) with bulk OUT, protocol preference 02 > 01 > 03
 *   2. Otherwise a vendor-specific (FF) interface with bulk OUT is reported as
 *      candidate (is_printer_class = false); the caller decides whether to use it.
 * Returns true if anything was found.
 */
bool usb_desc_find_printer_interface(const uint8_t *cfg, size_t len, usb_printer_if_info_t *out);

/* Collects all non-zero iConfiguration / iInterface string indices (deduplicated). */
size_t usb_desc_collect_string_indices(const uint8_t *cfg, size_t len, uint8_t *idx_out, size_t max);

/* Validates the bLength/bDescriptorType chain. Returns NULL if OK, else an error text. */
const char *usb_desc_validate_chain(const uint8_t *cfg, size_t len);

/*
 * Converts a raw USB string descriptor (bLength, 0x03, UTF-16LE...) to UTF-8.
 * Surrogate pairs are replaced by '?'. Returns number of bytes written (without NUL).
 */
size_t usb_str_desc_to_utf8(const uint8_t *desc, size_t desc_len, char *dst, size_t dst_size);

/*
 * Interprets the 2-byte length prefix of an IEEE 1284 Device ID returned by
 * GET_DEVICE_ID. The Printer Class spec defines it big-endian, but some devices
 * send little-endian; whichever is plausible for `received` bytes wins.
 * Copies the ID text (non-printable bytes replaced by '?') into dst.
 */
void usb_ieee1284_extract(const uint8_t *buf, size_t received, char *dst, size_t dst_size,
                          uint16_t *len_field, bool *little_endian);

/* Finds "KEY:value;" in an IEEE 1284 ID (keys compared case-insensitively). */
bool usb_ieee1284_get_field(const char *devid, const char *key, char *dst, size_t dst_size);

void usb_report_format_text(const usb_dev_report_t *r, strbuf_t *sb);
void usb_report_format_json(const usb_dev_report_t *r, strbuf_t *sb);

#ifdef __cplusplus
}
#endif
