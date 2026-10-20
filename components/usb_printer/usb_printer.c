#include "usb_printer.h"

#include <stddef.h>
#include <stdio.h>
#include <string.h>

#include "esp_check.h"
#include "esp_heap_caps.h"
#include "esp_intr_alloc.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "sdkconfig.h"
#include "usb/usb_host.h"

static const char *TAG = "usb_printer";

/* USB Printer Class 1.1, section 4.2: class specific requests */
#define PRN_REQ_GET_DEVICE_ID   0x00
#define PRN_REQ_GET_PORT_STATUS 0x01

#define CTRL_TIMEOUT_MS         2000
#define CTRL_DATA_MAX           USB_REPORT_DEVID_MAX
#define IN_BUF_SIZE             512
#define CANCEL_WAIT_MS          1000

typedef enum {
    EVT_NEW_DEV,
    EVT_DEV_GONE,
} mgr_evt_type_t;

typedef struct {
    mgr_evt_type_t type;
    uint8_t addr;
    usb_device_handle_t dev_hdl;
} mgr_evt_t;

static usb_host_client_handle_t s_client;
static QueueHandle_t s_evt_q;

/* s_state_lock protects s_report, s_summary and the handle fields below. */
static SemaphoreHandle_t s_state_lock;
/* s_io_lock serializes all transfers (EP0 and bulk) to the device. */
static SemaphoreHandle_t s_io_lock;

static usb_device_handle_t s_dev;
static volatile bool s_dev_gone;
static bool s_claimed;
static uint8_t s_claimed_if;

static usb_dev_report_t s_report;
static usb_printer_summary_t s_summary;

static usb_transfer_t *s_ctrl_xfer;
static usb_transfer_t *s_out_xfer;
static usb_transfer_t *s_in_xfer;
static SemaphoreHandle_t s_ctrl_done;
static SemaphoreHandle_t s_out_done;
static SemaphoreHandle_t s_in_done;
static volatile bool s_ctrl_inflight;

/* Text/JSON report buffers are large, keep them in PSRAM if available. */
#define REPORT_TEXT_SIZE (12 * 1024)
static char *s_print_buf;
static SemaphoreHandle_t s_print_lock;

/* ------------------------------------------------------------------------- */

static void set_err(char *dst, size_t sz, const char *what, esp_err_t err)
{
    snprintf(dst, sz, "%s: %s", what, esp_err_to_name(err));
}

static void set_last_error(const char *fmt, const char *detail)
{
    xSemaphoreTake(s_state_lock, portMAX_DELAY);
    snprintf(s_summary.last_error, sizeof(s_summary.last_error), fmt, detail);
    xSemaphoreGive(s_state_lock);
}

static const char *xfer_status_name(usb_transfer_status_t st)
{
    switch (st) {
    case USB_TRANSFER_STATUS_COMPLETED: return "COMPLETED";
    case USB_TRANSFER_STATUS_ERROR:     return "ERROR";
    case USB_TRANSFER_STATUS_TIMED_OUT: return "TIMED_OUT";
    case USB_TRANSFER_STATUS_CANCELED:  return "CANCELED";
    case USB_TRANSFER_STATUS_STALL:     return "STALL";
    case USB_TRANSFER_STATUS_OVERFLOW:  return "OVERFLOW";
    case USB_TRANSFER_STATUS_SKIPPED:   return "SKIPPED";
    case USB_TRANSFER_STATUS_NO_DEVICE: return "NO_DEVICE";
    default:                            return "?";
    }
}

static esp_err_t xfer_status_to_err(usb_transfer_status_t st)
{
    switch (st) {
    case USB_TRANSFER_STATUS_COMPLETED: return ESP_OK;
    case USB_TRANSFER_STATUS_NO_DEVICE: return ESP_ERR_NOT_FOUND;
    case USB_TRANSFER_STATUS_TIMED_OUT: return ESP_ERR_TIMEOUT;
    case USB_TRANSFER_STATUS_STALL:     return ESP_ERR_NOT_SUPPORTED;
    default:                            return ESP_FAIL;
    }
}

/* Transfer callbacks run in the context of the client task (usb_host_client_handle_events). */
static void xfer_cb(usb_transfer_t *xfer)
{
    if (xfer == s_ctrl_xfer) {
        s_ctrl_inflight = false;
    }
    xSemaphoreGive((SemaphoreHandle_t)xfer->context);
}

/* ------------------------------------------------------------------------- */
/* Control transfers (EP0)                                                   */
/* ------------------------------------------------------------------------- */

/*
 * Executes a control transfer. For IN requests the received data (without the
 * setup packet) is copied to `data`. Caller must hold s_io_lock.
 */
static esp_err_t ctrl_xfer(uint8_t bmRequestType, uint8_t bRequest, uint16_t wValue, uint16_t wIndex,
                           uint16_t wLength, uint8_t *data, size_t *actual)
{
    if (!s_dev || s_dev_gone) {
        return ESP_ERR_INVALID_STATE;
    }
    if (s_ctrl_inflight) {
        /* A previous control transfer never completed; the transfer object cannot be reused. */
        return ESP_ERR_INVALID_STATE;
    }
    if (wLength > CTRL_DATA_MAX) {
        return ESP_ERR_INVALID_SIZE;
    }
    usb_setup_packet_t *setup = (usb_setup_packet_t *)s_ctrl_xfer->data_buffer;
    setup->bmRequestType = bmRequestType;
    setup->bRequest = bRequest;
    setup->wValue = wValue;
    setup->wIndex = wIndex;
    setup->wLength = wLength;
    bool dir_in = bmRequestType & USB_BM_REQUEST_TYPE_DIR_IN;
    if (!dir_in && wLength && data) {
        memcpy(s_ctrl_xfer->data_buffer + sizeof(usb_setup_packet_t), data, wLength);
    }
    s_ctrl_xfer->num_bytes = sizeof(usb_setup_packet_t) + wLength;
    s_ctrl_xfer->device_handle = s_dev;
    s_ctrl_xfer->bEndpointAddress = 0;
    s_ctrl_xfer->callback = xfer_cb;
    s_ctrl_xfer->context = s_ctrl_done;
    s_ctrl_xfer->timeout_ms = CTRL_TIMEOUT_MS;

    xSemaphoreTake(s_ctrl_done, 0); /* drop stale give */
    s_ctrl_inflight = true;
    esp_err_t err = usb_host_transfer_submit_control(s_client, s_ctrl_xfer);
    if (err != ESP_OK) {
        s_ctrl_inflight = false;
        return err;
    }
    if (xSemaphoreTake(s_ctrl_done, pdMS_TO_TICKS(CTRL_TIMEOUT_MS)) != pdTRUE) {
        /* EP0 cannot be halted through the public API; the transfer stays marked in flight
         * until its callback arrives (device gone or late completion). */
        ESP_LOGW(TAG, "control request 0x%02x/0x%02x timed out", bmRequestType, bRequest);
        return ESP_ERR_TIMEOUT;
    }
    if (s_ctrl_xfer->status != USB_TRANSFER_STATUS_COMPLETED) {
        ESP_LOGW(TAG, "control request 0x%02x/0x%02x failed: %s", bmRequestType, bRequest,
                 xfer_status_name(s_ctrl_xfer->status));
        return xfer_status_to_err(s_ctrl_xfer->status);
    }
    size_t got = 0;
    if (s_ctrl_xfer->actual_num_bytes > (int)sizeof(usb_setup_packet_t)) {
        got = s_ctrl_xfer->actual_num_bytes - sizeof(usb_setup_packet_t);
    }
    if (dir_in && data) {
        if (got > wLength) {
            got = wLength;
        }
        memcpy(data, s_ctrl_xfer->data_buffer + sizeof(usb_setup_packet_t), got);
    }
    if (actual) {
        *actual = got;
    }
    return ESP_OK;
}

static esp_err_t get_string(uint8_t index, uint16_t lang, uint8_t *buf, size_t bufsz, size_t *got)
{
    return ctrl_xfer(USB_BM_REQUEST_TYPE_DIR_IN | USB_BM_REQUEST_TYPE_TYPE_STANDARD | USB_BM_REQUEST_TYPE_RECIP_DEVICE,
                     USB_B_REQUEST_GET_DESCRIPTOR, (uint16_t)((USB_B_DESCRIPTOR_TYPE_STRING << 8) | index), lang,
                     (uint16_t)(bufsz > 255 ? 255 : bufsz), buf, got);
}

static esp_err_t prn_get_device_id(uint8_t intf, uint8_t alt, uint8_t *buf, size_t bufsz, size_t *got)
{
    /* wValue = configuration index (zero based), wIndex = interface (high byte) | alternate setting (low byte) */
    return ctrl_xfer(USB_BM_REQUEST_TYPE_DIR_IN | USB_BM_REQUEST_TYPE_TYPE_CLASS | USB_BM_REQUEST_TYPE_RECIP_INTERFACE,
                     PRN_REQ_GET_DEVICE_ID, 0, (uint16_t)((intf << 8) | alt), (uint16_t)bufsz, buf, got);
}

static esp_err_t prn_get_port_status(uint8_t intf, uint8_t *status)
{
    size_t got = 0;
    esp_err_t err = ctrl_xfer(USB_BM_REQUEST_TYPE_DIR_IN | USB_BM_REQUEST_TYPE_TYPE_CLASS | USB_BM_REQUEST_TYPE_RECIP_INTERFACE,
                              PRN_REQ_GET_PORT_STATUS, 0, intf, 1, status, &got);
    if (err == ESP_OK && got != 1) {
        err = ESP_ERR_INVALID_RESPONSE;
    }
    return err;
}

static esp_err_t clear_endpoint_halt(uint8_t ep)
{
    return ctrl_xfer(USB_BM_REQUEST_TYPE_DIR_OUT | USB_BM_REQUEST_TYPE_TYPE_STANDARD | USB_BM_REQUEST_TYPE_RECIP_ENDPOINT,
                     USB_B_REQUEST_CLEAR_FEATURE, 0 /* ENDPOINT_HALT */, ep, 0, NULL, NULL);
}

/* ------------------------------------------------------------------------- */
/* Device setup                                                              */
/* ------------------------------------------------------------------------- */

/* Caller holds s_io_lock. Fills the class info of s_report (under s_state_lock). */
static void query_class_info_locked(void)
{
    usb_printer_if_info_t pif;
    xSemaphoreTake(s_state_lock, portMAX_DELAY);
    pif = s_report.pif;
    xSemaphoreGive(s_state_lock);
    if (!pif.found || !pif.is_printer_class) {
        return;
    }

    static uint8_t idbuf[USB_REPORT_DEVID_MAX];
    size_t got = 0;
    esp_err_t err = prn_get_device_id(pif.interface_number, pif.alternate_setting, idbuf, sizeof(idbuf), &got);
    uint8_t ps = 0;
    esp_err_t err_ps = prn_get_port_status(pif.interface_number, &ps);

    xSemaphoreTake(s_state_lock, portMAX_DELAY);
    s_report.devid_attempted = true;
    s_report.devid[0] = '\0';
    if (err == ESP_OK) {
        s_report.devid_err[0] = '\0';
        usb_ieee1284_extract(idbuf, got, s_report.devid, sizeof(s_report.devid),
                             &s_report.devid_len_field, &s_report.devid_len_little_endian);
    } else {
        set_err(s_report.devid_err, sizeof(s_report.devid_err), "GET_DEVICE_ID", err);
    }
    s_report.port_status_attempted = true;
    if (err_ps == ESP_OK) {
        s_report.port_status_err[0] = '\0';
        s_report.port_status = ps;
    } else {
        set_err(s_report.port_status_err, sizeof(s_report.port_status_err), "GET_PORT_STATUS", err_ps);
    }
    xSemaphoreGive(s_state_lock);
}

static void fetch_strings_locked(const uint8_t *cfg, size_t cfg_len)
{
    static uint8_t buf[256];
    size_t got = 0;
    uint16_t lang = 0;
    if (get_string(0, 0, buf, sizeof(buf), &got) == ESP_OK && got >= 4 && buf[1] == USB_B_DESCRIPTOR_TYPE_STRING) {
        lang = usb_rd16(&buf[2]);
    }
    uint8_t idx[USB_REPORT_MAX_EXTRA_STR];
    size_t n = usb_desc_collect_string_indices(cfg, cfg_len, idx, USB_REPORT_MAX_EXTRA_STR);

    usb_report_string_t tmp[USB_REPORT_MAX_EXTRA_STR];
    size_t ok = 0;
    for (size_t i = 0; i < n && lang != 0; i++) {
        if (get_string(idx[i], lang, buf, sizeof(buf), &got) == ESP_OK) {
            tmp[ok].index = idx[i];
            usb_str_desc_to_utf8(buf, got, tmp[ok].text, sizeof(tmp[ok].text));
            ok++;
        }
    }
    xSemaphoreTake(s_state_lock, portMAX_DELAY);
    s_report.lang_id = lang;
    memcpy(s_report.extra_strings, tmp, ok * sizeof(tmp[0]));
    s_report.n_extra_strings = (uint8_t)ok;
    xSemaphoreGive(s_state_lock);
}

static bool claim_allowed(const usb_printer_if_info_t *pif)
{
    if (!pif->found) {
        return false;
    }
    if (pif->is_printer_class) {
        return true;
    }
#if CONFIG_USB_PRINTER_ALLOW_VENDOR_INTERFACE
    return true;
#else
    return false;
#endif
}

static void handle_new_device(uint8_t addr)
{
    if (s_dev) {
        ESP_LOGW(TAG, "additional USB device at address %u ignored (only one printer supported)", addr);
        return;
    }
    usb_device_handle_t dev;
    esp_err_t err = usb_host_device_open(s_client, addr, &dev);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "usb_host_device_open(%u) failed: %s", addr, esp_err_to_name(err));
        return;
    }

    usb_device_info_t info;
    const usb_device_desc_t *dd = NULL;
    const usb_config_desc_t *cd = NULL;
    if (usb_host_device_info(dev, &info) != ESP_OK ||
        usb_host_get_device_descriptor(dev, &dd) != ESP_OK ||
        usb_host_get_active_config_descriptor(dev, &cd) != ESP_OK || !dd || !cd) {
        ESP_LOGE(TAG, "could not read descriptors of device %u", addr);
        usb_host_device_close(s_client, dev);
        return;
    }

    xSemaphoreTake(s_io_lock, portMAX_DELAY);
    xSemaphoreTake(s_state_lock, portMAX_DELAY);
    s_dev = dev;
    s_dev_gone = false;
    uint32_t cc = s_report.connect_count + 1;
    memset(&s_report, 0, sizeof(s_report));
    s_report.valid = true;
    s_report.connected = true;
    s_report.connect_count = cc;
    s_report.dev_addr = info.dev_addr;
    s_report.speed = (int)info.speed;
    s_report.active_config_value = info.bConfigurationValue;
    memcpy(s_report.dev_desc, dd, sizeof(s_report.dev_desc));
    s_report.cfg_total_len = cd->wTotalLength;
    s_report.cfg_len = cd->wTotalLength > USB_REPORT_MAX_CFG_LEN ? USB_REPORT_MAX_CFG_LEN : cd->wTotalLength;
    memcpy(s_report.cfg_desc, cd, s_report.cfg_len);
    usb_str_desc_to_utf8((const uint8_t *)info.str_desc_manufacturer,
                         info.str_desc_manufacturer ? info.str_desc_manufacturer->bLength : 0,
                         s_report.manufacturer, sizeof(s_report.manufacturer));
    usb_str_desc_to_utf8((const uint8_t *)info.str_desc_product,
                         info.str_desc_product ? info.str_desc_product->bLength : 0,
                         s_report.product, sizeof(s_report.product));
    usb_str_desc_to_utf8((const uint8_t *)info.str_desc_serial_num,
                         info.str_desc_serial_num ? info.str_desc_serial_num->bLength : 0,
                         s_report.serial, sizeof(s_report.serial));
    usb_desc_find_printer_interface(s_report.cfg_desc, s_report.cfg_len, &s_report.pif);

    memset(&s_summary, 0, offsetof(usb_printer_summary_t, connect_count));
    s_summary.connected = true;
    s_summary.vid = dd->idVendor;
    s_summary.pid = dd->idProduct;
    s_summary.connect_count = cc;
    s_summary.is_printer_class = s_report.pif.found && s_report.pif.is_printer_class;
    strlcpy(s_summary.manufacturer, s_report.manufacturer, sizeof(s_summary.manufacturer));
    strlcpy(s_summary.product, s_report.product, sizeof(s_summary.product));
    usb_printer_if_info_t pif = s_report.pif;
    xSemaphoreGive(s_state_lock);

    ESP_LOGI(TAG, "device attached: addr=%u VID=0x%04x PID=0x%04x speed=%s \"%s\" \"%s\"",
             info.dev_addr, dd->idVendor, dd->idProduct, usb_speed_name(info.speed),
             s_report.manufacturer, s_report.product);

    fetch_strings_locked((const uint8_t *)cd, s_report.cfg_len);

    if (pif.found) {
        ESP_LOGI(TAG, "%s interface %u alt %u: bulk OUT 0x%02x (MPS %u), bulk IN 0x%02x",
                 pif.is_printer_class ? "printer class" : "vendor-specific candidate",
                 pif.interface_number, pif.alternate_setting, pif.ep_out, pif.ep_out_mps, pif.ep_in);
    } else {
        ESP_LOGW(TAG, "no printer class or vendor interface with bulk OUT endpoint found");
    }

    char claim_err[40] = "";
    bool claimed = false;
    if (claim_allowed(&pif)) {
        err = usb_host_interface_claim(s_client, dev, pif.interface_number, pif.alternate_setting);
        if (err == ESP_OK && pif.alternate_setting != 0) {
            /* usb_host_interface_claim() does not send SET_INTERFACE itself */
            esp_err_t e2 = ctrl_xfer(USB_BM_REQUEST_TYPE_DIR_OUT | USB_BM_REQUEST_TYPE_TYPE_STANDARD | USB_BM_REQUEST_TYPE_RECIP_INTERFACE,
                                     USB_B_REQUEST_SET_INTERFACE, pif.alternate_setting, pif.interface_number, 0, NULL, NULL);
            if (e2 != ESP_OK) {
                ESP_LOGW(TAG, "SET_INTERFACE alt %u failed: %s", pif.alternate_setting, esp_err_to_name(e2));
            }
        }
        if (err == ESP_OK) {
            claimed = true;
            s_claimed = true;
            s_claimed_if = pif.interface_number;
        } else {
            set_err(claim_err, sizeof(claim_err), "claim", err);
            ESP_LOGE(TAG, "interface claim failed: %s", esp_err_to_name(err));
        }
    } else if (pif.found) {
        strlcpy(claim_err, "vendor interface not enabled (Kconfig)", sizeof(claim_err));
    }

    xSemaphoreTake(s_state_lock, portMAX_DELAY);
    s_report.claimed = claimed;
    strlcpy(s_report.claim_err, claim_err, sizeof(s_report.claim_err));
    s_summary.ready = claimed && pif.ep_out != 0;
    xSemaphoreGive(s_state_lock);

    query_class_info_locked();
    xSemaphoreGive(s_io_lock);

#if CONFIG_USB_PRINTER_DUMP_ON_CONNECT
    usb_printer_print_report();
#endif
}

static void handle_dev_gone(usb_device_handle_t hdl)
{
    if (!s_dev || hdl != s_dev) {
        return;
    }
    ESP_LOGW(TAG, "printer disconnected");
    s_dev_gone = true;
    /* Waiting for s_io_lock lets a writer observe NO_DEVICE and give up first. */
    xSemaphoreTake(s_io_lock, portMAX_DELAY);
    for (int i = 0; i < 50 && s_ctrl_inflight; i++) {
        vTaskDelay(pdMS_TO_TICKS(20));
    }
    if (s_claimed) {
        esp_err_t err = usb_host_interface_release(s_client, s_dev, s_claimed_if);
        if (err != ESP_OK) {
            ESP_LOGW(TAG, "interface release: %s", esp_err_to_name(err));
        }
        s_claimed = false;
    }
    esp_err_t err = usb_host_device_close(s_client, s_dev);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "device close: %s", esp_err_to_name(err));
    }
    xSemaphoreTake(s_state_lock, portMAX_DELAY);
    s_dev = NULL;
    s_report.connected = false;
    s_report.claimed = false;
    s_summary.connected = false;
    s_summary.ready = false;
    xSemaphoreGive(s_state_lock);
    xSemaphoreGive(s_io_lock);
}

/* ------------------------------------------------------------------------- */
/* Tasks                                                                     */
/* ------------------------------------------------------------------------- */

static void client_event_cb(const usb_host_client_event_msg_t *msg, void *arg)
{
    mgr_evt_t evt = { 0 };
    if (msg->event == USB_HOST_CLIENT_EVENT_NEW_DEV) {
        evt.type = EVT_NEW_DEV;
        evt.addr = msg->new_dev.address;
    } else if (msg->event == USB_HOST_CLIENT_EVENT_DEV_GONE) {
        evt.type = EVT_DEV_GONE;
        evt.dev_hdl = msg->dev_gone.dev_hdl;
        if (evt.dev_hdl == s_dev) {
            s_dev_gone = true; /* lets a running transfer loop stop early */
        }
    } else {
        return;
    }
    if (xQueueSend(s_evt_q, &evt, 0) != pdTRUE) {
        ESP_LOGE(TAG, "event queue full, USB event lost");
    }
}

static void usb_lib_task(void *arg)
{
    for (;;) {
        uint32_t flags = 0;
        usb_host_lib_handle_events(portMAX_DELAY, &flags);
        if (flags & USB_HOST_LIB_EVENT_FLAGS_NO_CLIENTS) {
            usb_host_device_free_all();
        }
    }
}

static void usb_client_task(void *arg)
{
    for (;;) {
        usb_host_client_handle_events(s_client, portMAX_DELAY);
    }
}

static void usb_mgr_task(void *arg)
{
    mgr_evt_t evt;
    for (;;) {
        if (xQueueReceive(s_evt_q, &evt, portMAX_DELAY) != pdTRUE) {
            continue;
        }
        if (evt.type == EVT_NEW_DEV) {
            handle_new_device(evt.addr);
        } else {
            handle_dev_gone(evt.dev_hdl);
        }
    }
}

/* ------------------------------------------------------------------------- */
/* Public API                                                                */
/* ------------------------------------------------------------------------- */

esp_err_t usb_printer_start(void)
{
    s_state_lock = xSemaphoreCreateMutex();
    s_io_lock = xSemaphoreCreateMutex();
    s_ctrl_done = xSemaphoreCreateBinary();
    s_out_done = xSemaphoreCreateBinary();
    s_in_done = xSemaphoreCreateBinary();
    s_print_lock = xSemaphoreCreateMutex();
    s_evt_q = xQueueCreate(8, sizeof(mgr_evt_t));
    s_print_buf = heap_caps_malloc(REPORT_TEXT_SIZE, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!s_print_buf) {
        s_print_buf = malloc(REPORT_TEXT_SIZE);
    }
    ESP_RETURN_ON_FALSE(s_state_lock && s_io_lock && s_ctrl_done && s_out_done && s_in_done && s_evt_q && s_print_buf &&
                        s_print_lock,
                        ESP_ERR_NO_MEM, TAG, "alloc");

    const usb_host_config_t host_cfg = {
        .skip_phy_setup = false,
        .intr_flags = ESP_INTR_FLAG_LEVEL1,
    };
    ESP_RETURN_ON_ERROR(usb_host_install(&host_cfg), TAG, "usb_host_install");

    const usb_host_client_config_t client_cfg = {
        .is_synchronous = false,
        .max_num_event_msg = 5,
        .async = {
            .client_event_callback = client_event_cb,
            .callback_arg = NULL,
        },
    };
    ESP_RETURN_ON_ERROR(usb_host_client_register(&client_cfg, &s_client), TAG, "client register");

    ESP_RETURN_ON_ERROR(usb_host_transfer_alloc(sizeof(usb_setup_packet_t) + CTRL_DATA_MAX, 0, &s_ctrl_xfer),
                        TAG, "ctrl xfer alloc");
    ESP_RETURN_ON_ERROR(usb_host_transfer_alloc(CONFIG_USB_PRINTER_BULK_CHUNK_SIZE, 0, &s_out_xfer),
                        TAG, "out xfer alloc");
    ESP_RETURN_ON_ERROR(usb_host_transfer_alloc(IN_BUF_SIZE, 0, &s_in_xfer), TAG, "in xfer alloc");

    BaseType_t ok = pdPASS;
    ok &= xTaskCreatePinnedToCore(usb_lib_task, "usb_lib", 4096, NULL, 10, NULL, 0);
    ok &= xTaskCreatePinnedToCore(usb_client_task, "usb_client", 4096, NULL, 9, NULL, 0);
    ok &= xTaskCreatePinnedToCore(usb_mgr_task, "usb_mgr", 6144, NULL, 6, NULL, tskNO_AFFINITY);
    ESP_RETURN_ON_FALSE(ok == pdPASS, ESP_ERR_NO_MEM, TAG, "task create");

    ESP_LOGI(TAG, "USB host started, waiting for printer on the native USB port (GPIO19/20)");
    return ESP_OK;
}

void usb_printer_get_summary(usb_printer_summary_t *out)
{
    xSemaphoreTake(s_state_lock, portMAX_DELAY);
    *out = s_summary;
    xSemaphoreGive(s_state_lock);
}

void usb_printer_report_text(strbuf_t *sb)
{
    xSemaphoreTake(s_state_lock, portMAX_DELAY);
    usb_report_format_text(&s_report, sb);
    xSemaphoreGive(s_state_lock);
}

void usb_printer_report_json(strbuf_t *sb)
{
    xSemaphoreTake(s_state_lock, portMAX_DELAY);
    usb_report_format_json(&s_report, sb);
    xSemaphoreGive(s_state_lock);
}

void usb_printer_print_report(void)
{
    xSemaphoreTake(s_print_lock, portMAX_DELAY);
    strbuf_t sb;
    sb_init(&sb, s_print_buf, REPORT_TEXT_SIZE);
    usb_printer_report_text(&sb);
    /* Written in pieces: printf of very large strings stalls the UART driver buffer. */
    for (size_t off = 0; off < sb.len; off += 256) {
        size_t n = sb.len - off < 256 ? sb.len - off : 256;
        fwrite(sb.buf + off, 1, n, stdout);
    }
    if (sb.overflow) {
        printf("\n[report truncated]\n");
    }
    fflush(stdout);
    xSemaphoreGive(s_print_lock);
}

esp_err_t usb_printer_refresh_class_info(void)
{
    xSemaphoreTake(s_io_lock, portMAX_DELAY);
    esp_err_t err = (s_dev && !s_dev_gone) ? ESP_OK : ESP_ERR_INVALID_STATE;
    if (err == ESP_OK) {
        query_class_info_locked();
    }
    xSemaphoreGive(s_io_lock);
    return err;
}

esp_err_t usb_printer_get_port_status(uint8_t *status)
{
    xSemaphoreTake(s_state_lock, portMAX_DELAY);
    usb_printer_if_info_t pif = s_report.pif;
    xSemaphoreGive(s_state_lock);
    if (!pif.found || !pif.is_printer_class) {
        return ESP_ERR_NOT_SUPPORTED;
    }
    xSemaphoreTake(s_io_lock, portMAX_DELAY);
    esp_err_t err = prn_get_port_status(pif.interface_number, status);
    xSemaphoreGive(s_io_lock);
    return err;
}

/*
 * Cancels the transfer in flight on `ep` and resynchronises host and device:
 * host pipe halt/flush/clear plus a standard CLEAR_FEATURE(ENDPOINT_HALT) to the
 * device, which resets the data toggle on both sides. Caller holds s_io_lock.
 */
static void cancel_and_recover(uint8_t ep, SemaphoreHandle_t done)
{
    if (usb_host_endpoint_halt(s_dev, ep) == ESP_OK) {
        usb_host_endpoint_flush(s_dev, ep);
    }
    if (xSemaphoreTake(done, pdMS_TO_TICKS(CANCEL_WAIT_MS)) != pdTRUE) {
        ESP_LOGE(TAG, "transfer on EP 0x%02x did not return after cancel", ep);
    }
    usb_host_endpoint_clear(s_dev, ep);
    if (!s_dev_gone) {
        esp_err_t err = clear_endpoint_halt(ep);
        if (err != ESP_OK) {
            ESP_LOGW(TAG, "CLEAR_FEATURE(ENDPOINT_HALT) 0x%02x: %s", ep, esp_err_to_name(err));
        }
    }
}

esp_err_t usb_printer_write(const uint8_t *data, size_t len, uint32_t timeout_ms, size_t *written)
{
    size_t done = 0;
    if (written) {
        *written = 0;
    }
    xSemaphoreTake(s_io_lock, portMAX_DELAY);
    xSemaphoreTake(s_state_lock, portMAX_DELAY);
    bool ready = s_summary.ready && s_dev && !s_dev_gone;
    uint8_t ep = s_report.pif.ep_out;
    xSemaphoreGive(s_state_lock);
    if (!ready) {
        xSemaphoreGive(s_io_lock);
        return ESP_ERR_INVALID_STATE;
    }

    esp_err_t err = ESP_OK;
    while (done < len) {
        if (s_dev_gone) {
            err = ESP_ERR_NOT_FOUND;
            break;
        }
        size_t chunk = len - done;
        if (chunk > s_out_xfer->data_buffer_size) {
            chunk = s_out_xfer->data_buffer_size;
        }
        memcpy(s_out_xfer->data_buffer, data + done, chunk);
        s_out_xfer->num_bytes = (int)chunk;
        s_out_xfer->device_handle = s_dev;
        s_out_xfer->bEndpointAddress = ep;
        s_out_xfer->callback = xfer_cb;
        s_out_xfer->context = s_out_done;
        s_out_xfer->flags = 0;
        s_out_xfer->timeout_ms = timeout_ms;
        xSemaphoreTake(s_out_done, 0);

        err = usb_host_transfer_submit(s_out_xfer);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "bulk OUT submit: %s", esp_err_to_name(err));
            break;
        }
        if (xSemaphoreTake(s_out_done, pdMS_TO_TICKS(timeout_ms)) != pdTRUE) {
            ESP_LOGE(TAG, "bulk OUT timeout after %lu ms (%u of %u bytes sent)", (unsigned long)timeout_ms,
                     (unsigned)done, (unsigned)len);
            cancel_and_recover(ep, s_out_done);
            if (s_out_xfer->actual_num_bytes > 0) {
                done += (size_t)s_out_xfer->actual_num_bytes;
            }
            err = ESP_ERR_TIMEOUT;
            break;
        }
        if (s_out_xfer->actual_num_bytes > 0) {
            done += (size_t)s_out_xfer->actual_num_bytes;
        }
        if (s_out_xfer->status != USB_TRANSFER_STATUS_COMPLETED) {
            ESP_LOGE(TAG, "bulk OUT failed: %s", xfer_status_name(s_out_xfer->status));
            err = xfer_status_to_err(s_out_xfer->status);
            if (s_out_xfer->status == USB_TRANSFER_STATUS_STALL && !s_dev_gone) {
                usb_host_endpoint_clear(s_dev, ep);
                clear_endpoint_halt(ep);
            }
            break;
        }
        if (s_out_xfer->actual_num_bytes != (int)chunk) {
            ESP_LOGE(TAG, "bulk OUT short write %d of %u", s_out_xfer->actual_num_bytes, (unsigned)chunk);
            err = ESP_FAIL;
            break;
        }
    }
    xSemaphoreGive(s_io_lock);

    xSemaphoreTake(s_state_lock, portMAX_DELAY);
    s_summary.bytes_written_total += done;
    if (err != ESP_OK) {
        s_summary.write_errors_total++;
    }
    xSemaphoreGive(s_state_lock);
    if (err != ESP_OK) {
        set_last_error("bulk OUT: %s", esp_err_to_name(err));
    }
    if (written) {
        *written = done;
    }
    return err;
}

esp_err_t usb_printer_read(uint8_t *buf, size_t len, size_t *got, uint32_t timeout_ms)
{
    *got = 0;
    xSemaphoreTake(s_io_lock, portMAX_DELAY);
    xSemaphoreTake(s_state_lock, portMAX_DELAY);
    bool ready = s_summary.ready && s_dev && !s_dev_gone;
    uint8_t ep = s_report.pif.ep_in;
    uint16_t mps = s_report.pif.ep_in_mps;
    xSemaphoreGive(s_state_lock);
    if (!ready || ep == 0 || mps == 0) {
        xSemaphoreGive(s_io_lock);
        return ready ? ESP_ERR_NOT_SUPPORTED : ESP_ERR_INVALID_STATE;
    }
    /* IN transfers must be a multiple of MPS */
    size_t n = (s_in_xfer->data_buffer_size / mps) * mps;
    s_in_xfer->num_bytes = (int)n;
    s_in_xfer->device_handle = s_dev;
    s_in_xfer->bEndpointAddress = ep;
    s_in_xfer->callback = xfer_cb;
    s_in_xfer->context = s_in_done;
    s_in_xfer->timeout_ms = timeout_ms;
    xSemaphoreTake(s_in_done, 0);

    esp_err_t err = usb_host_transfer_submit(s_in_xfer);
    if (err == ESP_OK) {
        if (xSemaphoreTake(s_in_done, pdMS_TO_TICKS(timeout_ms)) != pdTRUE) {
            /* No data: normal for many printers. Cancel the pending IN transfer. */
            cancel_and_recover(ep, s_in_done);
            err = ESP_ERR_TIMEOUT;
        } else if (s_in_xfer->status != USB_TRANSFER_STATUS_COMPLETED) {
            err = xfer_status_to_err(s_in_xfer->status);
        }
        int actual = s_in_xfer->actual_num_bytes;
        if (actual > 0) {
            size_t c = (size_t)actual < len ? (size_t)actual : len;
            memcpy(buf, s_in_xfer->data_buffer, c);
            *got = c;
            if (err == ESP_ERR_TIMEOUT) {
                err = ESP_OK;
            }
        }
    }
    xSemaphoreGive(s_io_lock);
    return err;
}
