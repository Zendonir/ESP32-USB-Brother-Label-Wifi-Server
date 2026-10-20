#include "usb_desc_report.h"

#include <ctype.h>
#include <stdio.h>
#include <string.h>

/* ------------------------------------------------------------------------- */
/* Names                                                                     */
/* ------------------------------------------------------------------------- */

const char *usb_class_name(uint8_t cls)
{
    switch (cls) {
    case 0x00: return "Defined at interface level";
    case 0x01: return "Audio";
    case 0x02: return "CDC Communications";
    case 0x03: return "HID";
    case 0x05: return "Physical";
    case 0x06: return "Image";
    case 0x07: return "Printer";
    case 0x08: return "Mass Storage";
    case 0x09: return "Hub";
    case 0x0a: return "CDC Data";
    case 0x0b: return "Smart Card";
    case 0x0d: return "Content Security";
    case 0x0e: return "Video";
    case 0x0f: return "Personal Healthcare";
    case 0x10: return "Audio/Video";
    case 0x11: return "Billboard";
    case 0xdc: return "Diagnostic";
    case 0xe0: return "Wireless Controller";
    case 0xef: return "Miscellaneous";
    case 0xfe: return "Application Specific";
    case 0xff: return "Vendor Specific";
    default:   return "Unknown";
    }
}

const char *usb_desc_type_name(uint8_t type)
{
    switch (type) {
    case USB_DT_DEVICE:             return "DEVICE";
    case USB_DT_CONFIGURATION:      return "CONFIGURATION";
    case USB_DT_STRING:             return "STRING";
    case USB_DT_INTERFACE:          return "INTERFACE";
    case USB_DT_ENDPOINT:           return "ENDPOINT";
    case USB_DT_DEVICE_QUALIFIER:   return "DEVICE_QUALIFIER";
    case USB_DT_OTHER_SPEED_CONFIG: return "OTHER_SPEED_CONFIGURATION";
    case USB_DT_INTERFACE_POWER:    return "INTERFACE_POWER";
    case USB_DT_OTG:                return "OTG";
    case USB_DT_DEBUG:              return "DEBUG";
    case USB_DT_IAD:                return "INTERFACE_ASSOCIATION";
    case 0x21:                      return "CLASS_SPECIFIC (0x21)";
    case 0x24:                      return "CS_INTERFACE";
    case 0x25:                      return "CS_ENDPOINT";
    default:                        return "UNKNOWN";
    }
}

const char *usb_speed_name(int speed)
{
    switch (speed) {
    case USB_REPORT_SPEED_LOW:  return "low";
    case USB_REPORT_SPEED_FULL: return "full";
    case USB_REPORT_SPEED_HIGH: return "high";
    default:                    return "unknown";
    }
}

static const char *ep_type_name(uint8_t attr)
{
    static const char *const names[] = { "control", "isochronous", "bulk", "interrupt" };
    return names[attr & 0x03];
}

static const char *printer_proto_name(uint8_t proto)
{
    switch (proto) {
    case USB_PRINTER_PROTO_UNIDIR: return "unidirectional";
    case USB_PRINTER_PROTO_BIDIR:  return "bidirectional";
    case USB_PRINTER_PROTO_1284_4: return "IEEE 1284.4 compatible bidirectional";
    default:                       return "unknown";
    }
}

static const char *lookup_string(const usb_dev_report_t *r, uint8_t index)
{
    if (index == 0) {
        return NULL;
    }
    for (size_t i = 0; i < r->n_extra_strings; i++) {
        if (r->extra_strings[i].index == index) {
            return r->extra_strings[i].text;
        }
    }
    return NULL;
}

/* ------------------------------------------------------------------------- */
/* Parsing                                                                   */
/* ------------------------------------------------------------------------- */

const char *usb_desc_validate_chain(const uint8_t *cfg, size_t len)
{
    if (len < 9) {
        return "configuration descriptor shorter than 9 bytes";
    }
    if (cfg[1] != USB_DT_CONFIGURATION) {
        return "first descriptor is not a configuration descriptor";
    }
    size_t off = 0;
    while (off < len) {
        if (len - off < 2) {
            return "trailing byte after last descriptor";
        }
        uint8_t blen = cfg[off];
        if (blen < 2) {
            return "descriptor with bLength < 2";
        }
        if (blen > len - off) {
            return "descriptor exceeds available data (truncated?)";
        }
        off += blen;
    }
    return NULL;
}

/* Iterates descriptors safely; returns false at end or on malformed data. */
static bool next_desc(const uint8_t *cfg, size_t len, size_t *off, const uint8_t **d)
{
    if (*off + 2 > len) {
        return false;
    }
    uint8_t blen = cfg[*off];
    if (blen < 2 || blen > len - *off) {
        return false;
    }
    *d = cfg + *off;
    *off += blen;
    return true;
}

static int score_interface(const usb_printer_if_info_t *c)
{
    if (c->ep_out == 0) {
        return 0;
    }
    if (c->interface_class == USB_IF_CLASS_PRINTER && c->interface_subclass == USB_PRINTER_SUBCLASS) {
        switch (c->interface_protocol) {
        case USB_PRINTER_PROTO_BIDIR:  return 30;
        case USB_PRINTER_PROTO_UNIDIR: return 20;
        case USB_PRINTER_PROTO_1284_4: return 10;
        default:                       return 5;
        }
    }
    if (c->interface_class == USB_IF_CLASS_VENDOR) {
        return 1;
    }
    return 0;
}

bool usb_desc_find_printer_interface(const uint8_t *cfg, size_t len, usb_printer_if_info_t *out)
{
    usb_printer_if_info_t best = { 0 };
    int best_score = 0;
    usb_printer_if_info_t cur = { 0 };
    bool in_if = false;

    size_t off = 0;
    const uint8_t *d;
    for (;;) {
        bool more = next_desc(cfg, len, &off, &d);
        if (!more || d[1] == USB_DT_INTERFACE) {
            if (in_if) {
                int s = score_interface(&cur);
                if (s > best_score) {
                    best_score = s;
                    best = cur;
                }
            }
            if (!more) {
                break;
            }
            if (d[0] < 9) {
                in_if = false;
                continue;
            }
            memset(&cur, 0, sizeof(cur));
            in_if = true;
            cur.interface_number = d[2];
            cur.alternate_setting = d[3];
            cur.interface_class = d[5];
            cur.interface_subclass = d[6];
            cur.interface_protocol = d[7];
        } else if (d[1] == USB_DT_ENDPOINT && in_if && d[0] >= 7) {
            uint8_t addr = d[2];
            uint8_t attr = d[3];
            uint16_t mps = usb_rd16(&d[4]) & 0x07ff;
            if ((attr & 0x03) == 0x02) { /* bulk */
                if (addr & 0x80) {
                    if (cur.ep_in == 0) {
                        cur.ep_in = addr;
                        cur.ep_in_mps = mps;
                    }
                } else if (cur.ep_out == 0) {
                    cur.ep_out = addr;
                    cur.ep_out_mps = mps;
                }
            }
        }
    }

    if (best_score > 0) {
        best.found = true;
        best.is_printer_class = best.interface_class == USB_IF_CLASS_PRINTER &&
                                best.interface_subclass == USB_PRINTER_SUBCLASS;
    }
    *out = best;
    return best.found;
}

size_t usb_desc_collect_string_indices(const uint8_t *cfg, size_t len, uint8_t *idx_out, size_t max)
{
    size_t n = 0;
    size_t off = 0;
    const uint8_t *d;
    while (next_desc(cfg, len, &off, &d)) {
        uint8_t idx = 0;
        if (d[1] == USB_DT_CONFIGURATION && d[0] >= 9) {
            idx = d[6];
        } else if (d[1] == USB_DT_INTERFACE && d[0] >= 9) {
            idx = d[8];
        } else if (d[1] == USB_DT_IAD && d[0] >= 8) {
            idx = d[7];
        }
        if (idx == 0) {
            continue;
        }
        bool dup = false;
        for (size_t i = 0; i < n; i++) {
            if (idx_out[i] == idx) {
                dup = true;
                break;
            }
        }
        if (!dup && n < max) {
            idx_out[n++] = idx;
        }
    }
    return n;
}

size_t usb_str_desc_to_utf8(const uint8_t *desc, size_t desc_len, char *dst, size_t dst_size)
{
    if (dst_size == 0) {
        return 0;
    }
    size_t o = 0;
    if (desc && desc_len >= 2 && desc[1] == USB_DT_STRING) {
        size_t blen = desc[0] < desc_len ? desc[0] : desc_len;
        for (size_t i = 2; i + 1 < blen; i += 2) {
            uint16_t cp = usb_rd16(&desc[i]);
            char tmp[3];
            size_t n;
            if (cp >= 0xd800 && cp <= 0xdfff) {
                tmp[0] = '?';
                n = 1;
            } else if (cp < 0x80) {
                tmp[0] = (char)cp;
                n = 1;
            } else if (cp < 0x800) {
                tmp[0] = (char)(0xc0 | (cp >> 6));
                tmp[1] = (char)(0x80 | (cp & 0x3f));
                n = 2;
            } else {
                tmp[0] = (char)(0xe0 | (cp >> 12));
                tmp[1] = (char)(0x80 | ((cp >> 6) & 0x3f));
                tmp[2] = (char)(0x80 | (cp & 0x3f));
                n = 3;
            }
            if (o + n >= dst_size) {
                break;
            }
            memcpy(dst + o, tmp, n);
            o += n;
        }
    }
    dst[o] = '\0';
    return o;
}

void usb_ieee1284_extract(const uint8_t *buf, size_t received, char *dst, size_t dst_size,
                          uint16_t *len_field, bool *little_endian)
{
    *len_field = 0;
    *little_endian = false;
    if (dst_size == 0) {
        return;
    }
    dst[0] = '\0';
    if (received < 2) {
        return;
    }
    uint16_t be = (uint16_t)((buf[0] << 8) | buf[1]);
    uint16_t le = (uint16_t)(buf[0] | (buf[1] << 8));
    uint16_t len;
    if (be >= 2 && be <= received) {
        len = be;
    } else if (le >= 2 && le <= received) {
        len = le;
        *little_endian = true;
    } else {
        /* Neither interpretation fits: use what was actually received. */
        len = (uint16_t)received;
        *little_endian = false;
    }
    *len_field = *little_endian ? le : be;

    size_t o = 0;
    for (size_t i = 2; i < len && o + 1 < dst_size; i++) {
        uint8_t c = buf[i];
        if (c == 0) {
            break;
        }
        dst[o++] = (c >= 0x20 && c < 0x7f) ? (char)c : '?';
    }
    dst[o] = '\0';
}

bool usb_ieee1284_get_field(const char *devid, const char *key, char *dst, size_t dst_size)
{
    size_t klen = strlen(key);
    const char *p = devid;
    while (p && *p) {
        while (*p == ' ') {
            p++;
        }
        const char *colon = strchr(p, ':');
        const char *semi = strchr(p, ';');
        if (!colon || (semi && semi < colon)) {
            p = semi ? semi + 1 : NULL;
            continue;
        }
        size_t this_klen = (size_t)(colon - p);
        bool match = this_klen == klen;
        for (size_t i = 0; match && i < klen; i++) {
            match = toupper((unsigned char)p[i]) == toupper((unsigned char)key[i]);
        }
        const char *val = colon + 1;
        const char *end = semi ? semi : val + strlen(val);
        if (match) {
            size_t n = (size_t)(end - val);
            if (n >= dst_size) {
                n = dst_size - 1;
            }
            memcpy(dst, val, n);
            dst[n] = '\0';
            return true;
        }
        p = semi ? semi + 1 : NULL;
    }
    if (dst_size) {
        dst[0] = '\0';
    }
    return false;
}

/* ------------------------------------------------------------------------- */
/* Text report                                                               */
/* ------------------------------------------------------------------------- */

static void text_string_ref(strbuf_t *sb, const usb_dev_report_t *r, uint8_t idx, const char *known)
{
    if (idx == 0) {
        sb_puts(sb, "0 (none)");
        return;
    }
    const char *s = known ? known : lookup_string(r, idx);
    sb_printf(sb, "%u \"%s\"", idx, s ? s : "?");
}

static void text_device(strbuf_t *sb, const usb_dev_report_t *r)
{
    const uint8_t *d = r->dev_desc;
    sb_puts(sb, "DEVICE DESCRIPTOR\n");
    sb_printf(sb, "  bLength            %u\n", d[0]);
    sb_printf(sb, "  bDescriptorType    0x%02x\n", d[1]);
    sb_printf(sb, "  bcdUSB             %x.%02x\n", d[3], d[2]);
    sb_printf(sb, "  bDeviceClass       0x%02x (%s)\n", d[4], usb_class_name(d[4]));
    sb_printf(sb, "  bDeviceSubClass    0x%02x\n", d[5]);
    sb_printf(sb, "  bDeviceProtocol    0x%02x\n", d[6]);
    sb_printf(sb, "  bMaxPacketSize0    %u\n", d[7]);
    sb_printf(sb, "  idVendor           0x%04x\n", usb_rd16(&d[8]));
    sb_printf(sb, "  idProduct          0x%04x\n", usb_rd16(&d[10]));
    sb_printf(sb, "  bcdDevice          %x.%02x\n", d[13], d[12]);
    sb_puts(sb, "  iManufacturer      ");
    text_string_ref(sb, r, d[14], r->manufacturer[0] ? r->manufacturer : NULL);
    sb_puts(sb, "\n  iProduct           ");
    text_string_ref(sb, r, d[15], r->product[0] ? r->product : NULL);
    sb_puts(sb, "\n  iSerialNumber      ");
    text_string_ref(sb, r, d[16], r->serial[0] ? r->serial : NULL);
    sb_printf(sb, "\n  bNumConfigurations %u\n", d[17]);
}

static void text_config(strbuf_t *sb, const usb_dev_report_t *r)
{
    const uint8_t *cfg = r->cfg_desc;
    size_t len = r->cfg_len;
    size_t off = 0;
    const uint8_t *d;
    const char *indent = "  ";
    while (next_desc(cfg, len, &off, &d)) {
        switch (d[1]) {
        case USB_DT_CONFIGURATION:
            if (d[0] < 9) {
                goto raw;
            }
            sb_puts(sb, "CONFIGURATION DESCRIPTOR\n");
            sb_printf(sb, "  wTotalLength        %u\n", usb_rd16(&d[2]));
            sb_printf(sb, "  bNumInterfaces      %u\n", d[4]);
            sb_printf(sb, "  bConfigurationValue %u\n", d[5]);
            sb_puts(sb, "  iConfiguration      ");
            text_string_ref(sb, r, d[6], NULL);
            sb_printf(sb, "\n  bmAttributes        0x%02x (%s%s)\n", d[7],
                      (d[7] & 0x40) ? "self-powered" : "bus-powered",
                      (d[7] & 0x20) ? ", remote wakeup" : "");
            sb_printf(sb, "  bMaxPower           %u mA\n", d[8] * 2u);
            break;
        case USB_DT_INTERFACE:
            if (d[0] < 9) {
                goto raw;
            }
            sb_printf(sb, "  INTERFACE %u ALT %u\n", d[2], d[3]);
            sb_printf(sb, "    bNumEndpoints      %u\n", d[4]);
            sb_printf(sb, "    bInterfaceClass    0x%02x (%s)\n", d[5], usb_class_name(d[5]));
            sb_printf(sb, "    bInterfaceSubClass 0x%02x\n", d[6]);
            sb_printf(sb, "    bInterfaceProtocol 0x%02x", d[7]);
            if (d[5] == USB_IF_CLASS_PRINTER) {
                sb_printf(sb, " (%s)", printer_proto_name(d[7]));
            }
            sb_puts(sb, "\n    iInterface         ");
            text_string_ref(sb, r, d[8], NULL);
            sb_putc(sb, '\n');
            indent = "    ";
            break;
        case USB_DT_ENDPOINT:
            if (d[0] < 7) {
                goto raw;
            }
            sb_printf(sb, "    ENDPOINT 0x%02x %s %s  wMaxPacketSize %u", d[2],
                      (d[2] & 0x80) ? "IN " : "OUT", ep_type_name(d[3]), usb_rd16(&d[4]) & 0x07ff);
            if (usb_rd16(&d[4]) & 0x1800) {
                sb_printf(sb, " (+%u extra transactions)", (usb_rd16(&d[4]) >> 11) & 0x3);
            }
            sb_printf(sb, "  bInterval %u  bmAttributes 0x%02x\n", d[6], d[3]);
            break;
        case USB_DT_IAD:
            if (d[0] < 8) {
                goto raw;
            }
            sb_printf(sb, "  INTERFACE ASSOCIATION first=%u count=%u class=0x%02x sub=0x%02x proto=0x%02x\n",
                      d[2], d[3], d[4], d[5], d[6]);
            break;
        default:
raw:
            sb_printf(sb, "%s%s type=0x%02x len=%u: ", indent, usb_desc_type_name(d[1]), d[1], d[0]);
            sb_hex(sb, d, d[0], ' ');
            sb_putc(sb, '\n');
            break;
        }
    }
}

void usb_report_format_text(const usb_dev_report_t *r, strbuf_t *sb)
{
    sb_puts(sb, "================ USB DEVICE REPORT ================\n");
    if (!r->valid) {
        sb_puts(sb, "No USB device has been enumerated since boot.\n");
        return;
    }
    sb_printf(sb, "State: %s   address: %u   speed: %s   connects since boot: %lu\n",
              r->connected ? "CONNECTED" : "DISCONNECTED (last seen device below)",
              r->dev_addr, usb_speed_name(r->speed), (unsigned long)r->connect_count);
    sb_printf(sb, "Active bConfigurationValue: %u   LANGID: 0x%04x\n\n", r->active_config_value, r->lang_id);

    text_device(sb, r);
    sb_putc(sb, '\n');

    const char *chain_err = usb_desc_validate_chain(r->cfg_desc, r->cfg_len);
    if (r->cfg_len < r->cfg_total_len) {
        sb_printf(sb, "WARNING: configuration descriptor truncated to %u of %u bytes\n",
                  r->cfg_len, r->cfg_total_len);
    }
    if (chain_err) {
        sb_printf(sb, "WARNING: descriptor chain: %s\n", chain_err);
    }
    text_config(sb, r);

    sb_puts(sb, "\nANALYSIS\n");
    const usb_printer_if_info_t *p = &r->pif;
    if (!p->found) {
        sb_puts(sb, "  No interface with a bulk OUT endpoint of class Printer (0x07) or Vendor (0xff) found.\n");
    } else {
        sb_printf(sb, "  %s: interface %u alt %u class 0x%02x/0x%02x/0x%02x\n",
                  p->is_printer_class ? "USB Printer Class interface" : "Vendor-specific CANDIDATE (not printer class)",
                  p->interface_number, p->alternate_setting, p->interface_class,
                  p->interface_subclass, p->interface_protocol);
        sb_printf(sb, "  bulk OUT: 0x%02x (MPS %u)\n", p->ep_out, p->ep_out_mps);
        if (p->ep_in) {
            sb_printf(sb, "  bulk IN : 0x%02x (MPS %u)\n", p->ep_in, p->ep_in_mps);
        } else {
            sb_puts(sb, "  bulk IN : none (no status back-channel on this interface)\n");
        }
        sb_printf(sb, "  claimed : %s%s%s\n", r->claimed ? "yes" : "no",
                  r->claim_err[0] ? " - " : "", r->claim_err);
    }
    {
        size_t off = 0;
        const uint8_t *d;
        while (next_desc(r->cfg_desc, r->cfg_len, &off, &d)) {
            if (d[1] == USB_DT_INTERFACE && d[0] >= 9 && d[5] == USB_IF_CLASS_MASS_STORAGE) {
                sb_printf(sb, "  NOTE: interface %u is Mass Storage - device may be in a storage/editor mode.\n", d[2]);
            }
        }
    }
    if (r->dev_desc[17] > 1) {
        sb_printf(sb, "  NOTE: device has %u configurations; only the active one is shown.\n", r->dev_desc[17]);
    }

    sb_puts(sb, "\nIEEE 1284 DEVICE ID (Printer Class GET_DEVICE_ID)\n");
    if (!r->devid_attempted) {
        sb_puts(sb, "  not requested (no printer class interface)\n");
    } else if (r->devid_err[0]) {
        sb_printf(sb, "  failed: %s\n", r->devid_err);
    } else {
        sb_printf(sb, "  length field: %u (%s)\n  %s\n", r->devid_len_field,
                  r->devid_len_little_endian ? "little-endian, non-standard" : "big-endian", r->devid);
    }

    sb_puts(sb, "\nPORT STATUS (Printer Class GET_PORT_STATUS)\n");
    if (!r->port_status_attempted) {
        sb_puts(sb, "  not requested\n");
    } else if (r->port_status_err[0]) {
        sb_printf(sb, "  failed: %s\n", r->port_status_err);
    } else {
        uint8_t s = r->port_status;
        sb_printf(sb, "  raw 0x%02x  paper_empty=%s  selected=%s  no_error=%s\n", s,
                  (s & 0x20) ? "yes" : "no", (s & 0x10) ? "yes" : "no", (s & 0x08) ? "yes" : "no");
    }

    sb_puts(sb, "\nRAW DEVICE DESCRIPTOR\n");
    sb_hexdump(sb, r->dev_desc, sizeof(r->dev_desc), "  ");
    sb_printf(sb, "RAW CONFIGURATION DESCRIPTOR (%u bytes)\n", r->cfg_len);
    sb_hexdump(sb, r->cfg_desc, r->cfg_len, "  ");
    sb_puts(sb, "===================================================\n");
}

/* ------------------------------------------------------------------------- */
/* JSON report                                                               */
/* ------------------------------------------------------------------------- */

static void json_hex(strbuf_t *sb, const char *key, const uint8_t *d, size_t n)
{
    sb_json_key(sb, key);
    sb_putc(sb, '"');
    sb_hex(sb, d, n, 0);
    sb_putc(sb, '"');
}

static void json_str_or_null(strbuf_t *sb, const char *s)
{
    if (s && s[0]) {
        sb_json_str(sb, s);
    } else {
        sb_puts(sb, "null");
    }
}

static void json_interface(strbuf_t *sb, const usb_dev_report_t *r, const uint8_t *ifd, size_t body_off, size_t body_end)
{
    sb_printf(sb, "{\"bInterfaceNumber\":%u,\"bAlternateSetting\":%u,\"bNumEndpoints\":%u,"
              "\"bInterfaceClass\":%u,\"bInterfaceSubClass\":%u,\"bInterfaceProtocol\":%u,\"iInterface\":%u,",
              ifd[2], ifd[3], ifd[4], ifd[5], ifd[6], ifd[7], ifd[8]);
    sb_json_key(sb, "class_name");
    sb_json_str(sb, usb_class_name(ifd[5]));
    sb_putc(sb, ',');
    sb_json_key(sb, "interface_string");
    json_str_or_null(sb, lookup_string(r, ifd[8]));

    sb_puts(sb, ",\"endpoints\":[");
    bool first = true;
    size_t off = body_off;
    const uint8_t *d;
    while (off < body_end && next_desc(r->cfg_desc, body_end, &off, &d)) {
        if (d[1] != USB_DT_ENDPOINT || d[0] < 7) {
            continue;
        }
        uint16_t w = usb_rd16(&d[4]);
        sb_printf(sb, "%s{\"bEndpointAddress\":%u,\"address_hex\":\"0x%02x\",\"direction\":\"%s\","
                  "\"type\":\"%s\",\"bmAttributes\":%u,\"wMaxPacketSize\":%u,\"bInterval\":%u}",
                  first ? "" : ",", d[2], d[2], (d[2] & 0x80) ? "in" : "out", ep_type_name(d[3]),
                  d[3], w & 0x07ff, d[6]);
        first = false;
    }
    sb_puts(sb, "],\"extra_descriptors\":[");
    first = true;
    off = body_off;
    while (off < body_end && next_desc(r->cfg_desc, body_end, &off, &d)) {
        if (d[1] == USB_DT_ENDPOINT && d[0] >= 7) {
            continue;
        }
        sb_printf(sb, "%s{\"bDescriptorType\":%u,\"type_name\":\"%s\",", first ? "" : ",", d[1],
                  usb_desc_type_name(d[1]));
        json_hex(sb, "raw", d, d[0]);
        sb_putc(sb, '}');
        first = false;
    }
    sb_puts(sb, "]}");
}

static void json_config(strbuf_t *sb, const usb_dev_report_t *r)
{
    const uint8_t *cfg = r->cfg_desc;
    size_t len = r->cfg_len;
    sb_puts(sb, "{");
    if (len >= 9 && cfg[1] == USB_DT_CONFIGURATION) {
        sb_printf(sb, "\"wTotalLength\":%u,\"bNumInterfaces\":%u,\"bConfigurationValue\":%u,"
                  "\"iConfiguration\":%u,\"bmAttributes\":%u,\"self_powered\":%s,\"remote_wakeup\":%s,"
                  "\"max_power_ma\":%u,",
                  usb_rd16(&cfg[2]), cfg[4], cfg[5], cfg[6], cfg[7],
                  (cfg[7] & 0x40) ? "true" : "false", (cfg[7] & 0x20) ? "true" : "false", cfg[8] * 2u);
        sb_json_key(sb, "configuration_string");
        json_str_or_null(sb, lookup_string(r, cfg[6]));
        sb_putc(sb, ',');
    }
    sb_printf(sb, "\"stored_length\":%u,\"truncated\":%s,", r->cfg_len,
              r->cfg_len < r->cfg_total_len ? "true" : "false");
    sb_json_key(sb, "chain_error");
    json_str_or_null(sb, usb_desc_validate_chain(cfg, len));
    sb_putc(sb, ',');
    json_hex(sb, "raw", cfg, len);

    sb_puts(sb, ",\"interfaces\":[");
    bool first_if = true;
    size_t off = 0;
    const uint8_t *d;
    while (next_desc(cfg, len, &off, &d)) {
        if (d[1] != USB_DT_INTERFACE || d[0] < 9) {
            continue;
        }
        /* body = everything up to the next interface descriptor */
        size_t body_off = off;
        size_t end = off;
        size_t scan = off;
        const uint8_t *e;
        while (next_desc(cfg, len, &scan, &e)) {
            if (e[1] == USB_DT_INTERFACE) {
                break;
            }
            end = scan;
        }
        if (!first_if) {
            sb_putc(sb, ',');
        }
        first_if = false;
        json_interface(sb, r, d, body_off, end);
        off = end;
    }
    sb_puts(sb, "],\"other_descriptors\":[");
    /* Descriptors between the configuration descriptor and the first interface (e.g. IAD, OTG) */
    bool first = true;
    off = 0;
    bool seen_cfg = false;
    while (next_desc(cfg, len, &off, &d)) {
        if (d[1] == USB_DT_INTERFACE) {
            break;
        }
        if (!seen_cfg && d[1] == USB_DT_CONFIGURATION) {
            seen_cfg = true;
            continue;
        }
        sb_printf(sb, "%s{\"bDescriptorType\":%u,\"type_name\":\"%s\",", first ? "" : ",", d[1],
                  usb_desc_type_name(d[1]));
        json_hex(sb, "raw", d, d[0]);
        sb_putc(sb, '}');
        first = false;
    }
    sb_puts(sb, "]}");
}

void usb_report_format_json(const usb_dev_report_t *r, strbuf_t *sb)
{
    sb_printf(sb, "{\"valid\":%s,\"connected\":%s,\"connect_count\":%lu",
              r->valid ? "true" : "false", r->connected ? "true" : "false", (unsigned long)r->connect_count);
    if (!r->valid) {
        sb_putc(sb, '}');
        return;
    }
    const uint8_t *d = r->dev_desc;
    sb_printf(sb, ",\"address\":%u,\"speed\":\"%s\",\"active_configuration\":%u,", r->dev_addr,
              usb_speed_name(r->speed), r->active_config_value);

    sb_printf(sb, "\"device\":{\"bcdUSB\":\"%x.%02x\",\"bDeviceClass\":%u,\"bDeviceSubClass\":%u,"
              "\"bDeviceProtocol\":%u,\"bMaxPacketSize0\":%u,\"idVendor\":%u,\"idProduct\":%u,"
              "\"vid_hex\":\"0x%04x\",\"pid_hex\":\"0x%04x\",\"bcdDevice\":\"%x.%02x\","
              "\"iManufacturer\":%u,\"iProduct\":%u,\"iSerialNumber\":%u,\"bNumConfigurations\":%u,",
              d[3], d[2], d[4], d[5], d[6], d[7], usb_rd16(&d[8]), usb_rd16(&d[10]),
              usb_rd16(&d[8]), usb_rd16(&d[10]), d[13], d[12], d[14], d[15], d[16], d[17]);
    sb_json_key(sb, "class_name");
    sb_json_str(sb, usb_class_name(d[4]));
    sb_putc(sb, ',');
    json_hex(sb, "raw", d, sizeof(r->dev_desc));
    sb_puts(sb, "},");

    sb_printf(sb, "\"strings\":{\"lang_id\":%u,", r->lang_id);
    sb_json_key(sb, "manufacturer");
    json_str_or_null(sb, r->manufacturer);
    sb_putc(sb, ',');
    sb_json_key(sb, "product");
    json_str_or_null(sb, r->product);
    sb_putc(sb, ',');
    sb_json_key(sb, "serial");
    json_str_or_null(sb, r->serial);
    sb_puts(sb, ",\"other\":[");
    for (size_t i = 0; i < r->n_extra_strings; i++) {
        sb_printf(sb, "%s{\"index\":%u,\"text\":", i ? "," : "", r->extra_strings[i].index);
        sb_json_str(sb, r->extra_strings[i].text);
        sb_putc(sb, '}');
    }
    sb_puts(sb, "]},");

    sb_json_key(sb, "configuration");
    json_config(sb, r);
    sb_putc(sb, ',');

    const usb_printer_if_info_t *p = &r->pif;
    sb_printf(sb, "\"printer_interface\":{\"found\":%s", p->found ? "true" : "false");
    if (p->found) {
        sb_printf(sb, ",\"is_printer_class\":%s,\"interface_number\":%u,\"alternate_setting\":%u,"
                  "\"class\":%u,\"subclass\":%u,\"protocol\":%u,\"ep_out\":%u,\"ep_out_mps\":%u,"
                  "\"ep_in\":%u,\"ep_in_mps\":%u",
                  p->is_printer_class ? "true" : "false", p->interface_number, p->alternate_setting,
                  p->interface_class, p->interface_subclass, p->interface_protocol,
                  p->ep_out, p->ep_out_mps, p->ep_in, p->ep_in_mps);
        if (p->is_printer_class) {
            sb_puts(sb, ",\"protocol_name\":");
            sb_json_str(sb, printer_proto_name(p->interface_protocol));
        }
    }
    sb_printf(sb, ",\"claimed\":%s,\"claim_error\":", r->claimed ? "true" : "false");
    json_str_or_null(sb, r->claim_err);
    sb_puts(sb, "},");

    sb_printf(sb, "\"ieee1284\":{\"attempted\":%s,\"error\":", r->devid_attempted ? "true" : "false");
    json_str_or_null(sb, r->devid_err);
    if (r->devid_attempted && !r->devid_err[0]) {
        sb_printf(sb, ",\"length_field\":%u,\"little_endian\":%s,\"id\":", r->devid_len_field,
                  r->devid_len_little_endian ? "true" : "false");
        sb_json_str(sb, r->devid);
        sb_puts(sb, ",\"fields\":{");
        static const char *const keys[][2] = {
            { "MFG", "MANUFACTURER" }, { "MDL", "MODEL" }, { "CMD", "COMMAND SET" },
            { "CLS", "CLASS" }, { "DES", "DESCRIPTION" }, { "SN", "SERIALNUMBER" },
        };
        bool first = true;
        for (size_t i = 0; i < sizeof(keys) / sizeof(keys[0]); i++) {
            char val[256];
            if (usb_ieee1284_get_field(r->devid, keys[i][0], val, sizeof(val)) ||
                usb_ieee1284_get_field(r->devid, keys[i][1], val, sizeof(val))) {
                sb_printf(sb, "%s\"%s\":", first ? "" : ",", keys[i][0]);
                sb_json_str(sb, val);
                first = false;
            }
        }
        sb_putc(sb, '}');
    }
    sb_puts(sb, "},");

    sb_printf(sb, "\"port_status\":{\"attempted\":%s,\"error\":", r->port_status_attempted ? "true" : "false");
    json_str_or_null(sb, r->port_status_err);
    if (r->port_status_attempted && !r->port_status_err[0]) {
        uint8_t s = r->port_status;
        sb_printf(sb, ",\"raw\":%u,\"paper_empty\":%s,\"selected\":%s,\"no_error\":%s", s,
                  (s & 0x20) ? "true" : "false", (s & 0x10) ? "true" : "false", (s & 0x08) ? "true" : "false");
    }
    sb_puts(sb, "}}");
}
