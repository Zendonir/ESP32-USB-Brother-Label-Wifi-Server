/*
 * strbuf - bounded string builder with JSON helpers.
 *
 * Pure C (no ESP-IDF dependencies) so it can be unit-tested on the host.
 * The buffer is always NUL-terminated; on overflow the output is truncated
 * and `overflow` is set.
 */
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    char *buf;
    size_t cap;
    size_t len;
    bool overflow;
} strbuf_t;

void sb_init(strbuf_t *sb, char *buf, size_t cap);
void sb_reset(strbuf_t *sb);
void sb_puts(strbuf_t *sb, const char *s);
void sb_putc(strbuf_t *sb, char c);
void sb_printf(strbuf_t *sb, const char *fmt, ...) __attribute__((format(printf, 2, 3)));

/* Hex dump "0a 1b 2c" (sep != 0) or "0a1b2c" (sep == 0). */
void sb_hex(strbuf_t *sb, const uint8_t *data, size_t len, char sep);

/* Classic 16-bytes-per-line hex dump with offset and ASCII column. */
void sb_hexdump(strbuf_t *sb, const uint8_t *data, size_t len, const char *indent);

/* JSON string literal including the surrounding quotes. */
void sb_json_str(strbuf_t *sb, const char *s);
void sb_json_strn(strbuf_t *sb, const char *s, size_t n);

/* `"key":` */
void sb_json_key(strbuf_t *sb, const char *key);

#ifdef __cplusplus
}
#endif
