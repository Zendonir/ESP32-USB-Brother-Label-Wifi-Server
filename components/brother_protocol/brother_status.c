#include "brother_status.h"

#include <string.h>

bool brother_status_parse(const uint8_t *d, size_t len, brother_status_t *out)
{
    if (len != BROTHER_STATUS_LEN || d[0] != 0x80 || d[1] != 0x20 || d[2] != 'B') {
        return false;
    }
    memset(out, 0, sizeof(*out));
    memcpy(out->raw, d, BROTHER_STATUS_LEN);
    out->series_code = (char)d[3];
    out->model_code = (char)d[4];
    out->battery = d[6];
    out->error1 = d[8];
    out->error2 = d[9];
    out->media_width_mm = d[10];
    out->media_type = d[11];
    out->mode = d[15];
    out->media_length_mm = d[17];
    out->status_type = d[18];
    out->phase_type = d[19];
    out->phase_number = (uint16_t)((d[20] << 8) | d[21]);
    out->notification = d[22];
    return true;
}

const char *brother_model_name(char model_code)
{
    switch (model_code) {
    case '3': return "TD-2020";
    case '5': return "TD-2120N";
    case '6': return "TD-2130N";
    case 'D': return "TD-2030A";
    case 'E': return "TD-2125N";
    case 'F': return "TD-2125NWB";
    case 'G': return "TD-2135N";
    case 'H': return "TD-2135NWB";
    default:  return "unknown";
    }
}

const char *brother_status_type_name(uint8_t t)
{
    switch (t) {
    case BROTHER_ST_REPLY:         return "reply_to_status_request";
    case BROTHER_ST_PRINTING_DONE: return "printing_completed";
    case BROTHER_ST_ERROR:         return "error_occurred";
    case 0x03:                     return "exit_if_mode";
    case BROTHER_ST_TURNED_OFF:    return "turned_off";
    case BROTHER_ST_NOTIFICATION:  return "notification";
    case BROTHER_ST_PHASE_CHANGE:  return "phase_change";
    default:                       return "unknown";
    }
}

const char *brother_media_type_name(uint8_t m)
{
    switch (m) {
    case BROTHER_MEDIA_NONE:       return "none";
    case BROTHER_MEDIA_CONTINUOUS: return "continuous_tape";
    case BROTHER_MEDIA_DIE_CUT:    return "die_cut_labels";
    default:                       return "unknown";
    }
}

const char *brother_phase_name(uint8_t p)
{
    switch (p) {
    case BROTHER_PHASE_RECEIVING: return "receiving";
    case BROTHER_PHASE_PRINTING:  return "printing";
    default:                      return "unknown";
    }
}

const char *brother_notification_name(uint8_t n)
{
    switch (n) {
    case 0x00: return "none";
    case 0x03: return "cooling_started";
    case 0x04: return "cooling_finished";
    case 0x05: return "waiting_for_peeling";
    case 0x06: return "finished_waiting_for_peeling";
    case 0x07: return "printer_paused";
    case 0x08: return "finished_printer_pause";
    default:   return "unknown";
    }
}

const char *brother_battery_name(uint8_t b)
{
    switch (b) {
    case 0x00: return "full";
    case 0x01: return "half";
    case 0x02: return "low";
    case 0x03: return "charging_required";
    case 0x04: return "ac_adapter";
    default:   return "unknown";
    }
}

static const struct {
    int byte;   /* 1 or 2 */
    uint8_t mask;
    const char *key;
    const char *text;
} k_errors[] = {
    { 1, BROTHER_ERR1_NO_MEDIA, "no_media", "No media" },
    { 1, BROTHER_ERR1_END_OF_MEDIA, "end_of_media", "End of media" },
    { 2, BROTHER_ERR2_WRONG_MEDIA, "wrong_media", "Wrong media" },
    { 2, BROTHER_ERR2_COMM_ERROR, "communication_error", "Communication error" },
    { 2, BROTHER_ERR2_COVER_OPEN, "cover_open", "Cover open" },
    { 2, BROTHER_ERR2_CANNOT_FEED, "cannot_feed", "Media cannot be fed" },
    { 2, BROTHER_ERR2_SYSTEM_ERROR, "system_error", "System error" },
};

static bool error_active(const brother_status_t *st, size_t i)
{
    uint8_t v = k_errors[i].byte == 1 ? st->error1 : st->error2;
    return (v & k_errors[i].mask) != 0;
}

bool brother_status_has_error(const brother_status_t *st)
{
    for (size_t i = 0; i < sizeof(k_errors) / sizeof(k_errors[0]); i++) {
        if (error_active(st, i)) {
            return true;
        }
    }
    return false;
}

void brother_status_errors_text(const brother_status_t *st, strbuf_t *sb)
{
    bool any = false;
    for (size_t i = 0; i < sizeof(k_errors) / sizeof(k_errors[0]); i++) {
        if (error_active(st, i)) {
            sb_printf(sb, "%s%s", any ? ", " : "", k_errors[i].text);
            any = true;
        }
    }
    if (!any) {
        sb_puts(sb, "none");
    }
}

void brother_status_format_json(const brother_status_t *st, strbuf_t *sb)
{
    sb_printf(sb, "{\"model_code\":\"%c\",\"model\":\"%s\",\"status_type\":\"%s\",\"phase\":\"%s\","
              "\"phase_number\":%u,\"notification\":\"%s\",\"battery\":\"%s\","
              "\"media_type\":\"%s\",\"media_width_mm\":%u,\"media_length_mm\":%u,\"mode\":%u,"
              "\"error1\":%u,\"error2\":%u,\"printer_in_use\":%s,\"has_error\":%s,\"errors\":[",
              st->model_code >= 0x20 && st->model_code < 0x7f ? st->model_code : '?',
              brother_model_name(st->model_code), brother_status_type_name(st->status_type),
              brother_phase_name(st->phase_type), st->phase_number, brother_notification_name(st->notification),
              brother_battery_name(st->battery), brother_media_type_name(st->media_type),
              st->media_width_mm, st->media_length_mm, st->mode, st->error1, st->error2,
              (st->error1 & BROTHER_ERR1_PRINTER_IN_USE) ? "true" : "false",
              brother_status_has_error(st) ? "true" : "false");
    bool first = true;
    for (size_t i = 0; i < sizeof(k_errors) / sizeof(k_errors[0]); i++) {
        if (error_active(st, i)) {
            sb_printf(sb, "%s\"%s\"", first ? "" : ",", k_errors[i].key);
            first = false;
        }
    }
    sb_puts(sb, "],\"raw\":\"");
    sb_hex(sb, st->raw, BROTHER_STATUS_LEN, 0);
    sb_puts(sb, "\"}");
}

void brother_status_format_text(const brother_status_t *st, strbuf_t *sb)
{
    sb_printf(sb, "Model        : %s (code '%c')\n", brother_model_name(st->model_code),
              st->model_code >= 0x20 && st->model_code < 0x7f ? st->model_code : '?');
    sb_printf(sb, "Status type  : %s\n", brother_status_type_name(st->status_type));
    sb_printf(sb, "Phase        : %s (%u)\n", brother_phase_name(st->phase_type), st->phase_number);
    sb_printf(sb, "Media        : %s, %u mm", brother_media_type_name(st->media_type), st->media_width_mm);
    if (st->media_length_mm) {
        sb_printf(sb, " x %u mm", st->media_length_mm);
    }
    sb_puts(sb, "\nErrors       : ");
    brother_status_errors_text(st, sb);
    sb_printf(sb, "%s\n", (st->error1 & BROTHER_ERR1_PRINTER_IN_USE) ? " (printer in use)" : "");
    sb_printf(sb, "Notification : %s\nPower        : %s\nRaw          : ",
              brother_notification_name(st->notification), brother_battery_name(st->battery));
    sb_hex(sb, st->raw, BROTHER_STATUS_LEN, ' ');
    sb_putc(sb, '\n');
}

size_t brother_build_status_query(uint8_t *buf, size_t size)
{
    const size_t n = BROTHER_INVALIDATE_LEN + 2 + 4 + 3;
    if (size < n) {
        return 0;
    }
    memset(buf, BROTHER_CMD_INVALIDATE, BROTHER_INVALIDATE_LEN);
    memcpy(buf + BROTHER_INVALIDATE_LEN, BROTHER_CMD_INITIALIZE, 2);
    memcpy(buf + BROTHER_INVALIDATE_LEN + 2, BROTHER_CMD_RASTER_MODE, 4);
    memcpy(buf + BROTHER_INVALIDATE_LEN + 6, BROTHER_CMD_STATUS_REQUEST, 3);
    return n;
}

/* ------------------------------------------------------------------------- */
/* Settings (static commands)                                                */
/* ------------------------------------------------------------------------- */

const brother_setting_def_t brother_settings[] = {
    { 'i', "command_mode", "Command mode", 3, { 0x00, 0x01, 0x03 },
      { "ESC/P", "Raster", "P-touch Template" } },
    { 'q', "print_priority", "Print priority", 2, { 0x00, 0x01 },
      { "Speed", "Quality" } },
    { 'h', "rotate", "Rotate print 180 degrees", 2, { 0x00, 0x01 },
      { "No rotation", "180 degrees" } },
    { '^', "stop_position", "Print stop position", 2, { 0x00, 0x01 },
      { "Tear bar", "Head" } },
    { 'd', "recovery", "Reprint after an error (recovery print)", 2, { 0x00, 0x01 },
      { "Off", "On" } },
};
const size_t brother_settings_count = sizeof(brother_settings) / sizeof(brother_settings[0]);

const brother_setting_def_t *brother_setting_find(const char *key)
{
    for (size_t i = 0; i < brother_settings_count; i++) {
        if (strcmp(brother_settings[i].key, key) == 0) {
            return &brother_settings[i];
        }
    }
    return NULL;
}

bool brother_setting_value_valid(const brother_setting_def_t *def, uint8_t value)
{
    for (uint8_t i = 0; i < def->n_options; i++) {
        if (def->values[i] == value) {
            return true;
        }
    }
    return false;
}

const char *brother_setting_label(const brother_setting_def_t *def, int value)
{
    for (uint8_t i = 0; i < def->n_options; i++) {
        if (def->values[i] == value) {
            return def->labels[i];
        }
    }
    return "unknown";
}

size_t brother_build_settings_prefix(uint8_t *buf, size_t size)
{
    const size_t n = BROTHER_INVALIDATE_LEN + 2 + 4;
    if (size < n) {
        return 0;
    }
    memset(buf, BROTHER_CMD_INVALIDATE, BROTHER_INVALIDATE_LEN);
    memcpy(buf + BROTHER_INVALIDATE_LEN, BROTHER_CMD_INITIALIZE, 2);
    memcpy(buf + BROTHER_INVALIDATE_LEN + 2, "\x1b\x69\x61\x01", 4);
    return n;
}

size_t brother_build_setting_get(char cmd, uint8_t *buf, size_t size)
{
    if (size < 7) {
        return 0;
    }
    const uint8_t c[7] = { 0x1b, 0x69, 0x58, (uint8_t)cmd, 0x31, 0x00, 0x00 };
    memcpy(buf, c, sizeof(c));
    return sizeof(c);
}

size_t brother_build_setting_set(char cmd, uint8_t value, uint8_t *buf, size_t size)
{
    if (size < 8) {
        return 0;
    }
    const uint8_t c[8] = { 0x1b, 0x69, 0x58, (uint8_t)cmd, 0x32, 0x01, 0x00, value };
    memcpy(buf, c, sizeof(c));
    return sizeof(c);
}

bool brother_setting_parse_reply(const uint8_t *data, size_t len, uint8_t *value)
{
    if (len != 3 || data[0] != 0x01 || data[1] != 0x00) {
        return false;
    }
    *value = data[2];
    return true;
}
