#include "strbuf.h"

#include <stdarg.h>
#include <stdio.h>
#include <string.h>

void sb_init(strbuf_t *sb, char *buf, size_t cap)
{
    sb->buf = buf;
    sb->cap = cap;
    sb_reset(sb);
}

void sb_reset(strbuf_t *sb)
{
    sb->len = 0;
    sb->overflow = false;
    if (sb->cap > 0) {
        sb->buf[0] = '\0';
    }
}

static void sb_write(strbuf_t *sb, const char *s, size_t n)
{
    if (sb->cap == 0) {
        sb->overflow = true;
        return;
    }
    size_t room = sb->cap - 1 - sb->len;
    if (n > room) {
        n = room;
        sb->overflow = true;
    }
    memcpy(sb->buf + sb->len, s, n);
    sb->len += n;
    sb->buf[sb->len] = '\0';
}

void sb_puts(strbuf_t *sb, const char *s)
{
    sb_write(sb, s, strlen(s));
}

void sb_putc(strbuf_t *sb, char c)
{
    sb_write(sb, &c, 1);
}

void sb_printf(strbuf_t *sb, const char *fmt, ...)
{
    if (sb->cap == 0) {
        sb->overflow = true;
        return;
    }
    size_t room = sb->cap - sb->len;
    va_list ap;
    va_start(ap, fmt);
    int n = vsnprintf(sb->buf + sb->len, room, fmt, ap);
    va_end(ap);
    if (n < 0) {
        sb->buf[sb->len] = '\0';
        sb->overflow = true;
        return;
    }
    if ((size_t)n >= room) {
        sb->len = sb->cap - 1;
        sb->overflow = true;
    } else {
        sb->len += (size_t)n;
    }
}

void sb_hex(strbuf_t *sb, const uint8_t *data, size_t len, char sep)
{
    static const char hexd[] = "0123456789abcdef";
    for (size_t i = 0; i < len; i++) {
        if (sep && i > 0) {
            sb_putc(sb, sep);
        }
        char pair[2] = { hexd[data[i] >> 4], hexd[data[i] & 0x0f] };
        sb_write(sb, pair, 2);
    }
}

void sb_hexdump(strbuf_t *sb, const uint8_t *data, size_t len, const char *indent)
{
    for (size_t off = 0; off < len; off += 16) {
        size_t n = (len - off) < 16 ? (len - off) : 16;
        sb_printf(sb, "%s%04x: ", indent, (unsigned)off);
        for (size_t i = 0; i < 16; i++) {
            if (i < n) {
                sb_printf(sb, "%02x ", data[off + i]);
            } else {
                sb_puts(sb, "   ");
            }
        }
        sb_puts(sb, " |");
        for (size_t i = 0; i < n; i++) {
            uint8_t c = data[off + i];
            sb_putc(sb, (c >= 0x20 && c < 0x7f) ? (char)c : '.');
        }
        sb_puts(sb, "|\n");
    }
}

void sb_json_strn(strbuf_t *sb, const char *s, size_t n)
{
    sb_putc(sb, '"');
    for (size_t i = 0; i < n && s[i] != '\0'; i++) {
        unsigned char c = (unsigned char)s[i];
        switch (c) {
        case '"':  sb_puts(sb, "\\\""); break;
        case '\\': sb_puts(sb, "\\\\"); break;
        case '\n': sb_puts(sb, "\\n"); break;
        case '\r': sb_puts(sb, "\\r"); break;
        case '\t': sb_puts(sb, "\\t"); break;
        default:
            if (c < 0x20 || c == 0x7f) {
                sb_printf(sb, "\\u%04x", c);
            } else {
                sb_putc(sb, (char)c); /* UTF-8 passes through unchanged */
            }
        }
    }
    sb_putc(sb, '"');
}

void sb_json_str(strbuf_t *sb, const char *s)
{
    sb_json_strn(sb, s ? s : "", (size_t)-1);
}

void sb_json_key(strbuf_t *sb, const char *key)
{
    sb_json_str(sb, key);
    sb_putc(sb, ':');
}
