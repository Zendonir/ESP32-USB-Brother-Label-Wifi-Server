/*
 * Host unit tests for the Brother status parser. The sample replies are built
 * from the field table in the Brother Raster Command Reference (TD-2000 series,
 * chapter 4, "ESC i S"); they are not captured from the printer.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "brother_status.h"

static int g_fail;
#define CHECK(c) do { if (!(c)) { fprintf(stderr, "%s:%d: CHECK failed: %s\n", __FILE__, __LINE__, #c); g_fail++; } } while (0)

static void sample(uint8_t *d, uint8_t err1, uint8_t err2, uint8_t width, uint8_t type, uint8_t length,
                   uint8_t status_type, uint8_t phase)
{
    memset(d, 0, BROTHER_STATUS_LEN);
    d[0] = 0x80; d[1] = 0x20; d[2] = 'B'; d[3] = '5'; d[4] = '3'; d[5] = '0';
    d[6] = 0x04; /* AC adaptor */
    d[8] = err1; d[9] = err2; d[10] = width; d[11] = type; d[14] = 0x3f;
    d[17] = length; d[18] = status_type; d[19] = phase;
}

int main(int argc, char **argv)
{
    const char *outdir = argc > 1 ? argv[1] : ".";
    uint8_t d[BROTHER_STATUS_LEN];
    brother_status_t st;
    static char buf[4096];
    strbuf_t sb;

    /* idle, 51 x 26 mm die-cut labels */
    sample(d, 0, 0, 51, BROTHER_MEDIA_DIE_CUT, 26, BROTHER_ST_REPLY, BROTHER_PHASE_RECEIVING);
    CHECK(brother_status_parse(d, sizeof(d), &st));
    CHECK(st.model_code == '3' && strcmp(brother_model_name(st.model_code), "TD-2020") == 0);
    CHECK(st.media_width_mm == 51 && st.media_length_mm == 26);
    CHECK(strcmp(brother_media_type_name(st.media_type), "die_cut_labels") == 0);
    CHECK(!brother_status_has_error(&st));
    sb_init(&sb, buf, sizeof(buf));
    brother_status_errors_text(&st, &sb);
    CHECK(strcmp(buf, "none") == 0);

    /* cover open + no media; "printer in use" alone is not an error */
    sample(d, BROTHER_ERR1_NO_MEDIA | BROTHER_ERR1_PRINTER_IN_USE, BROTHER_ERR2_COVER_OPEN, 0, 0, 0,
           BROTHER_ST_ERROR, BROTHER_PHASE_RECEIVING);
    CHECK(brother_status_parse(d, sizeof(d), &st));
    CHECK(brother_status_has_error(&st));
    sb_init(&sb, buf, sizeof(buf));
    brother_status_errors_text(&st, &sb);
    CHECK(strcmp(buf, "No media, Cover open") == 0);
    sample(d, BROTHER_ERR1_PRINTER_IN_USE, 0, 58, BROTHER_MEDIA_CONTINUOUS, 0, BROTHER_ST_PHASE_CHANGE,
           BROTHER_PHASE_PRINTING);
    CHECK(brother_status_parse(d, sizeof(d), &st));
    CHECK(!brother_status_has_error(&st));

    /* JSON output for validation by run_tests.sh */
    sample(d, 0, BROTHER_ERR2_COVER_OPEN, 58, BROTHER_MEDIA_CONTINUOUS, 0, BROTHER_ST_ERROR, 0);
    CHECK(brother_status_parse(d, sizeof(d), &st));
    sb_init(&sb, buf, sizeof(buf));
    brother_status_format_json(&st, &sb);
    CHECK(!sb.overflow);
    char path[512];
    snprintf(path, sizeof(path), "%s/brother_status.json", outdir);
    FILE *f = fopen(path, "w");
    fwrite(sb.buf, 1, sb.len, f);
    fclose(f);
    sb_init(&sb, buf, sizeof(buf));
    brother_status_format_text(&st, &sb);
    CHECK(!sb.overflow && strstr(buf, "Cover open") != NULL);

    /* rejects wrong length / header */
    CHECK(!brother_status_parse(d, 31, &st));
    d[2] = 'X';
    CHECK(!brother_status_parse(d, sizeof(d), &st));

    /* documented query sequence: 200 x 00h, ESC @, ESC i S */
    uint8_t q[256];
    size_t n = brother_build_status_query(q, sizeof(q));
    CHECK(n == 205);
    int zeros = 1;
    for (int i = 0; i < 200; i++) {
        zeros &= q[i] == 0x00;
    }
    CHECK(zeros);
    CHECK(memcmp(q + 200, "\x1b\x40\x1b\x69\x53", 5) == 0);
    CHECK(brother_build_status_query(q, 100) == 0);

    if (g_fail) {
        fprintf(stderr, "%d check(s) FAILED\n", g_fail);
        return 1;
    }
    printf("brother status tests passed\n");
    return 0;
}
