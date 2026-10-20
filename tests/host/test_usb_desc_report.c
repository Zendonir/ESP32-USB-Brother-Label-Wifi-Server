/*
 * Host unit tests for the USB descriptor parser / report formatter.
 *
 * IMPORTANT: the descriptors below are SYNTHETIC examples built from the
 * USB 2.0 and USB Printer Class 1.1 specifications. They are NOT the
 * descriptors of a Brother TD-2020A (those must be read from the real device
 * with the diagnostic firmware).
 *
 * Build & run: tests/host/run_tests.sh
 */
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "strbuf.h"
#include "usb_desc_report.h"

static int g_fail;
static const char *g_outdir = ".";

#define CHECK(cond)                                                              \
    do {                                                                         \
        if (!(cond)) {                                                           \
            fprintf(stderr, "%s:%d: CHECK failed: %s\n", __FILE__, __LINE__, #cond); \
            g_fail++;                                                            \
        }                                                                        \
    } while (0)

static char big[64 * 1024];

static void dump_json(const char *name, const usb_dev_report_t *r)
{
    strbuf_t sb;
    sb_init(&sb, big, sizeof(big));
    usb_report_format_json(r, &sb);
    CHECK(!sb.overflow);
    char path[512];
    snprintf(path, sizeof(path), "%s/%s.json", g_outdir, name);
    FILE *f = fopen(path, "w");
    assert(f);
    fwrite(sb.buf, 1, sb.len, f);
    fclose(f);

    sb_reset(&sb);
    usb_report_format_text(r, &sb);
    CHECK(!sb.overflow);
    snprintf(path, sizeof(path), "%s/%s.txt", g_outdir, name);
    f = fopen(path, "w");
    assert(f);
    fwrite(sb.buf, 1, sb.len, f);
    fclose(f);
}

static void fill_report(usb_dev_report_t *r, const uint8_t *dev, const uint8_t *cfg, size_t cfg_len)
{
    memset(r, 0, sizeof(*r));
    r->valid = true;
    r->connected = true;
    r->connect_count = 1;
    r->dev_addr = 1;
    r->speed = USB_REPORT_SPEED_FULL;
    r->active_config_value = 1;
    memcpy(r->dev_desc, dev, 18);
    memcpy(r->cfg_desc, cfg, cfg_len);
    r->cfg_len = (uint16_t)cfg_len;
    r->cfg_total_len = usb_rd16(&cfg[2]);
    usb_desc_find_printer_interface(r->cfg_desc, r->cfg_len, &r->pif);
}

/* Synthetic device descriptor: VID/PID 0x1234/0x5678 are placeholders. */
static const uint8_t DEV[18] = {
    18, 0x01, 0x00, 0x02, 0x00, 0x00, 0x00, 64,
    0x34, 0x12, 0x78, 0x56, 0x00, 0x01, 1, 2, 3, 1,
};

/* One printer-class interface, bidirectional, bulk OUT 0x02 + bulk IN 0x81 */
static const uint8_t CFG_BIDIR[] = {
    9, 0x02, 32, 0, 1, 1, 0, 0xc0, 1,
    9, 0x04, 0, 0, 2, 0x07, 0x01, 0x02, 0,
    7, 0x05, 0x02, 0x02, 64, 0, 0,
    7, 0x05, 0x81, 0x02, 64, 0, 0,
};

/* alt 0: unidirectional (OUT only), alt 1: bidirectional (OUT + IN), plus a mass storage interface */
static const uint8_t CFG_ALTS[] = {
    9, 0x02, 76, 0, 2, 1, 4, 0x80, 50,
    9, 0x04, 0, 0, 1, 0x07, 0x01, 0x01, 5,
    7, 0x05, 0x01, 0x02, 64, 0, 0,
    9, 0x04, 0, 1, 2, 0x07, 0x01, 0x02, 6,
    7, 0x05, 0x01, 0x02, 64, 0, 0,
    7, 0x05, 0x82, 0x02, 64, 0, 0,
    9, 0x04, 1, 0, 2, 0x08, 0x06, 0x50, 0,
    7, 0x05, 0x03, 0x02, 64, 0, 0,
    7, 0x05, 0x84, 0x02, 64, 0, 0,
    5, 0x24, 0x01, 0x02, 0x03,        /* unknown class-specific descriptor */
};

/* Only a vendor specific interface with interrupt IN and bulk OUT */
static const uint8_t CFG_VENDOR[] = {
    9, 0x02, 32, 0, 1, 1, 0, 0x40, 0,
    9, 0x04, 0, 0, 2, 0xff, 0x00, 0x00, 0,
    7, 0x05, 0x83, 0x03, 8, 0, 10,
    7, 0x05, 0x04, 0x02, 64, 0, 0,
};

/* HID only: no printer candidate */
static const uint8_t CFG_HID[] = {
    9, 0x02, 34, 0, 1, 1, 0, 0xa0, 50,
    9, 0x04, 0, 0, 1, 0x03, 0x01, 0x02, 0,
    9, 0x21, 0x11, 0x01, 0x00, 0x01, 0x22, 0x40, 0x00,
    7, 0x05, 0x81, 0x03, 8, 0, 10,
};

static void test_bidir(void)
{
    usb_dev_report_t *r = calloc(1, sizeof(*r));
    fill_report(r, DEV, CFG_BIDIR, sizeof(CFG_BIDIR));
    CHECK(r->pif.found);
    CHECK(r->pif.is_printer_class);
    CHECK(r->pif.interface_number == 0);
    CHECK(r->pif.alternate_setting == 0);
    CHECK(r->pif.interface_protocol == 2);
    CHECK(r->pif.ep_out == 0x02 && r->pif.ep_out_mps == 64);
    CHECK(r->pif.ep_in == 0x81 && r->pif.ep_in_mps == 64);
    CHECK(usb_desc_validate_chain(r->cfg_desc, r->cfg_len) == NULL);
    strcpy(r->manufacturer, "Example \"Mfr\"");
    strcpy(r->product, "Synthetic Printer");
    r->devid_attempted = true;
    strcpy(r->devid, "MFG:Example;CMD:FOO,BAR;MDL:Synthetic 1;CLS:PRINTER;");
    r->port_status_attempted = true;
    r->port_status = 0x18;
    dump_json("bidir", r);
    free(r);
}

static void test_alts(void)
{
    usb_dev_report_t *r = calloc(1, sizeof(*r));
    fill_report(r, DEV, CFG_ALTS, sizeof(CFG_ALTS));
    CHECK(r->pif.found && r->pif.is_printer_class);
    CHECK(r->pif.alternate_setting == 1);   /* bidirectional preferred */
    CHECK(r->pif.ep_out == 0x01);
    CHECK(r->pif.ep_in == 0x82);
    uint8_t idx[8];
    size_t n = usb_desc_collect_string_indices(r->cfg_desc, r->cfg_len, idx, 8);
    CHECK(n == 3 && idx[0] == 4 && idx[1] == 5 && idx[2] == 6);
    r->extra_strings[0] = (usb_report_string_t){ 5, "Printer Uni" };
    r->extra_strings[1] = (usb_report_string_t){ 6, "Printer Bi" };
    r->n_extra_strings = 2;
    r->devid_attempted = true;
    strcpy(r->devid_err, "GET_DEVICE_ID: ESP_ERR_NOT_SUPPORTED");
    dump_json("alts", r);
    free(r);
}

static void test_vendor(void)
{
    usb_printer_if_info_t p;
    CHECK(usb_desc_find_printer_interface(CFG_VENDOR, sizeof(CFG_VENDOR), &p));
    CHECK(!p.is_printer_class);
    CHECK(p.interface_class == 0xff);
    CHECK(p.ep_out == 0x04);
    CHECK(p.ep_in == 0);        /* interrupt IN is not a bulk IN */
    usb_dev_report_t *r = calloc(1, sizeof(*r));
    fill_report(r, DEV, CFG_VENDOR, sizeof(CFG_VENDOR));
    dump_json("vendor", r);
    free(r);
}

static void test_hid(void)
{
    usb_printer_if_info_t p;
    CHECK(!usb_desc_find_printer_interface(CFG_HID, sizeof(CFG_HID), &p));
    CHECK(!p.found);
    usb_dev_report_t *r = calloc(1, sizeof(*r));
    fill_report(r, DEV, CFG_HID, sizeof(CFG_HID));
    dump_json("hid", r);
    free(r);
}

static void test_malformed(void)
{
    const uint8_t zero_len[] = { 9, 0x02, 20, 0, 1, 1, 0, 0x80, 50, 0, 0x04, 0, 0, 0, 0, 0, 0, 0, 0, 0 };
    CHECK(usb_desc_validate_chain(zero_len, sizeof(zero_len)) != NULL);
    const uint8_t truncated[] = { 9, 0x02, 18, 0, 1, 1, 0, 0x80, 50, 9, 0x04, 0, 0 };
    CHECK(usb_desc_validate_chain(truncated, sizeof(truncated)) != NULL);
    usb_printer_if_info_t p;
    usb_desc_find_printer_interface(zero_len, sizeof(zero_len), &p);
    usb_desc_find_printer_interface(truncated, sizeof(truncated), &p);
    CHECK(!p.found);
    usb_dev_report_t *r = calloc(1, sizeof(*r));
    fill_report(r, DEV, truncated, sizeof(truncated));
    dump_json("malformed", r);
    free(r);

    usb_dev_report_t *empty = calloc(1, sizeof(*empty));
    dump_json("empty", empty);
    free(empty);
}

static void test_fuzz(void)
{
    /* Random descriptor soup: must never crash or read out of bounds (run with ASan/UBSan). */
    srand(12345);
    usb_dev_report_t *r = calloc(1, sizeof(*r));
    strbuf_t sb;
    for (int it = 0; it < 20000; it++) {
        uint8_t buf[200];
        size_t len = (size_t)(rand() % (int)sizeof(buf));
        for (size_t i = 0; i < len; i++) {
            buf[i] = (uint8_t)rand();
        }
        if (len > 2 && (rand() & 1)) {
            buf[1] = 0x02;
        }
        memset(r, 0, sizeof(*r));
        r->valid = true;
        memcpy(r->cfg_desc, buf, len);
        r->cfg_len = (uint16_t)len;
        for (size_t i = 0; i < 18; i++) {
            r->dev_desc[i] = (uint8_t)rand();
        }
        usb_desc_find_printer_interface(r->cfg_desc, r->cfg_len, &r->pif);
        usb_desc_validate_chain(r->cfg_desc, r->cfg_len);
        uint8_t idx[8];
        usb_desc_collect_string_indices(r->cfg_desc, r->cfg_len, idx, 8);
        sb_init(&sb, big, sizeof(big));
        usb_report_format_json(r, &sb);
        sb_reset(&sb);
        usb_report_format_text(r, &sb);
        char s[64];
        usb_str_desc_to_utf8(buf, len, s, sizeof(s));
        uint16_t lf;
        bool le;
        usb_ieee1284_extract(buf, len, s, sizeof(s), &lf, &le);
    }
    free(r);
}

static void test_strings(void)
{
    /* "Bä€" as UTF-16LE string descriptor */
    const uint8_t sd[] = { 8, 0x03, 'B', 0, 0xe4, 0x00, 0xac, 0x20 };
    char out[16];
    size_t n = usb_str_desc_to_utf8(sd, sizeof(sd), out, sizeof(out));
    CHECK(n == 6);
    CHECK(strcmp(out, "B\xc3\xa4\xe2\x82\xac") == 0);
    /* Output buffer too small: must truncate on a character boundary */
    char tiny[3];
    usb_str_desc_to_utf8(sd, sizeof(sd), tiny, sizeof(tiny));
    CHECK(strcmp(tiny, "B") == 0);
    /* bLength larger than the data actually received */
    const uint8_t lying[] = { 40, 0x03, 'A', 0 };
    usb_str_desc_to_utf8(lying, sizeof(lying), out, sizeof(out));
    CHECK(strcmp(out, "A") == 0);
}

static void test_ieee1284(void)
{
    const char *id = "MFG:Example;MDL:Model X;CMD:AAA,BBB;";
    uint8_t buf[128];
    size_t l = strlen(id) + 2;
    buf[0] = (uint8_t)(l >> 8);
    buf[1] = (uint8_t)l;
    memcpy(buf + 2, id, strlen(id));
    char out[128];
    uint16_t lf;
    bool le;
    usb_ieee1284_extract(buf, l, out, sizeof(out), &lf, &le);
    CHECK(!le && lf == l);
    CHECK(strcmp(out, id) == 0);

    buf[0] = (uint8_t)l; /* little-endian variant */
    buf[1] = 0;
    usb_ieee1284_extract(buf, l, out, sizeof(out), &lf, &le);
    CHECK(le && lf == l);
    CHECK(strcmp(out, id) == 0);

    char v[32];
    CHECK(usb_ieee1284_get_field(out, "mdl", v, sizeof(v)) && strcmp(v, "Model X") == 0);
    CHECK(usb_ieee1284_get_field(out, "CMD", v, sizeof(v)) && strcmp(v, "AAA,BBB") == 0);
    CHECK(!usb_ieee1284_get_field(out, "CLS", v, sizeof(v)));
    CHECK(usb_ieee1284_get_field("MFG:NoSemicolon", "MFG", v, sizeof(v)) && strcmp(v, "NoSemicolon") == 0);
}

static void test_strbuf(void)
{
    char b[8];
    strbuf_t sb;
    sb_init(&sb, b, sizeof(b));
    sb_puts(&sb, "abc");
    sb_printf(&sb, "%d", 12345);
    CHECK(sb.overflow);
    CHECK(strlen(b) == 7 && strcmp(b, "abc1234") == 0);
    char j[64];
    sb_init(&sb, j, sizeof(j));
    sb_json_str(&sb, "a\"b\\c\n\x01");
    CHECK(strcmp(j, "\"a\\\"b\\\\c\\n\\u0001\"") == 0);
}

int main(int argc, char **argv)
{
    if (argc > 1) {
        g_outdir = argv[1];
    }
    test_strbuf();
    test_strings();
    test_ieee1284();
    test_bidir();
    test_alts();
    test_vendor();
    test_hid();
    test_malformed();
    test_fuzz();
    if (g_fail) {
        fprintf(stderr, "%d check(s) FAILED\n", g_fail);
        return 1;
    }
    printf("all host tests passed\n");
    return 0;
}
