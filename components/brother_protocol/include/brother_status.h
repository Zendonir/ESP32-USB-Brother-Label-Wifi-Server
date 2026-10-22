/*
 * Brother TD-2000 series status information (32-byte reply).
 *
 * Source: Brother "Software Developer's Manual - Raster Command Reference,
 * TD-2020/2120N/2130N, TD-2030A/2125N/2125NWB/2135N/2135NWB", version 1.01,
 * chapter 4 "ESC i S Status information request". Verified to apply to the
 * connected TD-2020A (it enumerates as TD-2020, PID 0x2055).
 *
 * Pure C (no ESP-IDF dependencies) so it can be unit-tested on the host.
 */
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "strbuf.h"

#ifdef __cplusplus
extern "C" {
#endif

#define BROTHER_STATUS_LEN          32
#define BROTHER_INVALIDATE_LEN      200     /* "Sends a 200-byte invalidate command" */

/* Command bytes (Raster Command Reference, chapter 3 "Print Command List") */
#define BROTHER_CMD_INVALIDATE      0x00
#define BROTHER_CMD_INITIALIZE      "\x1b\x40"      /* ESC @ */
#define BROTHER_CMD_STATUS_REQUEST  "\x1b\x69\x53"  /* ESC i S */

/* Error information 1 (offset 8) */
#define BROTHER_ERR1_NO_MEDIA       0x01
#define BROTHER_ERR1_END_OF_MEDIA   0x02    /* die-cut labels only */
#define BROTHER_ERR1_PRINTER_IN_USE 0x10

/* Error information 2 (offset 9) */
#define BROTHER_ERR2_WRONG_MEDIA    0x01    /* "Replace media" error (serial) / wrong media */
#define BROTHER_ERR2_COMM_ERROR     0x04
#define BROTHER_ERR2_COVER_OPEN     0x10
#define BROTHER_ERR2_CANNOT_FEED    0x40    /* also when the media end is detected */
#define BROTHER_ERR2_SYSTEM_ERROR   0x80

/* Status type (offset 18) */
#define BROTHER_ST_REPLY            0x00
#define BROTHER_ST_PRINTING_DONE    0x01
#define BROTHER_ST_ERROR            0x02
#define BROTHER_ST_TURNED_OFF       0x04
#define BROTHER_ST_NOTIFICATION     0x05
#define BROTHER_ST_PHASE_CHANGE     0x06

/* Phase type (offset 19) */
#define BROTHER_PHASE_RECEIVING     0x00
#define BROTHER_PHASE_PRINTING      0x01

/* Media type (offset 11) */
#define BROTHER_MEDIA_NONE          0x00
#define BROTHER_MEDIA_CONTINUOUS    0x4a
#define BROTHER_MEDIA_DIE_CUT       0x4b

typedef struct {
    uint8_t raw[BROTHER_STATUS_LEN];
    char series_code;       /* offset 3, fixed '5' */
    char model_code;        /* offset 4, TD-2020: '3' */
    uint8_t battery;        /* offset 6 */
    uint8_t error1;         /* offset 8 */
    uint8_t error2;         /* offset 9 */
    uint8_t media_width_mm; /* offset 10 */
    uint8_t media_type;     /* offset 11 */
    uint8_t mode;           /* offset 15 */
    uint8_t media_length_mm;/* offset 17, 0 for continuous tape */
    uint8_t status_type;    /* offset 18 */
    uint8_t phase_type;     /* offset 19 */
    uint16_t phase_number;  /* offsets 20 (high) / 21 (low) */
    uint8_t notification;   /* offset 22 */
} brother_status_t;

/*
 * Validates the fixed header (80h, 20h, 'B') and decodes the fields.
 * Returns false if `len` != 32 or the header does not match.
 */
bool brother_status_parse(const uint8_t *data, size_t len, brother_status_t *out);

const char *brother_model_name(char model_code);
const char *brother_status_type_name(uint8_t status_type);
const char *brother_media_type_name(uint8_t media_type);
const char *brother_phase_name(uint8_t phase_type);
const char *brother_notification_name(uint8_t notification);
const char *brother_battery_name(uint8_t battery);

/* True if any error bit is set in error1/error2 (the "printer in use" bit is not an error). */
bool brother_status_has_error(const brother_status_t *st);

/*
 * Appends a human readable list of the active errors ("No media, Cover open").
 * Appends "none" if there is no error.
 */
void brother_status_errors_text(const brother_status_t *st, strbuf_t *sb);

void brother_status_format_json(const brother_status_t *st, strbuf_t *sb);
void brother_status_format_text(const brother_status_t *st, strbuf_t *sb);

/*
 * Builds the documented status query sequence into `buf`:
 * 200 x invalidate (00h), initialize (ESC @), status information request (ESC i S).
 * Returns the number of bytes written (205) or 0 if `size` is too small.
 */
size_t brother_build_status_query(uint8_t *buf, size_t size);

/*
 * Printer settings stored in the printer's non-volatile memory ("static
 * commands", P-touch Template 2.0 Command Reference 3.08, chapter 8, for TD-20XX):
 *   set:      ESC i X <cmd> 2 01h 00h <value>
 *   retrieve: ESC i X <cmd> 1 00h 00h  ->  reply 01h 00h <value>
 * Only settings with a documented command are listed. Power, density, sensor
 * adjustment and the media definition have no documented command; they stay in
 * the Brother "Printer Setting Tool" (media: Paper Size Setup in the driver).
 */
#define BROTHER_SETTING_MAX_OPTIONS 4

typedef struct {
    char cmd;                   /* the letter after ESC i X */
    const char *key;            /* API name */
    const char *name;           /* English label */
    uint8_t n_options;
    uint8_t values[BROTHER_SETTING_MAX_OPTIONS];
    const char *labels[BROTHER_SETTING_MAX_OPTIONS];
} brother_setting_def_t;

extern const brother_setting_def_t brother_settings[];
extern const size_t brother_settings_count;

const brother_setting_def_t *brother_setting_find(const char *key);
bool brother_setting_value_valid(const brother_setting_def_t *def, uint8_t value);
const char *brother_setting_label(const brother_setting_def_t *def, int value);

/*
 * Prefix for a settings session: invalidate, initialize and switch to raster
 * mode (ESC i a 01h). The static ESC i X commands are only executed in raster
 * mode (P-touch Template Command Reference, 5.1 and 6.3); the reference's
 * step back to template mode is left out because this server prints raster.
 * Returns the number of bytes (206) or 0 if `size` is too small.
 */
size_t brother_build_settings_prefix(uint8_t *buf, size_t size);
/* ESC i X <cmd> 1 00h 00h (7 bytes) */
size_t brother_build_setting_get(char cmd, uint8_t *buf, size_t size);
/* ESC i X <cmd> 2 01h 00h <value> (8 bytes) */
size_t brother_build_setting_set(char cmd, uint8_t value, uint8_t *buf, size_t size);
/* Reply 01h 00h <value>. False for anything else (e.g. a 32-byte status message). */
bool brother_setting_parse_reply(const uint8_t *data, size_t len, uint8_t *value);

#ifdef __cplusplus
}
#endif
