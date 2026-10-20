#include "diag_log.h"

#include <stdarg.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "esp_heap_caps.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"

#define LINE_MAX_LEN 256

static char *s_ring;
static size_t s_size;
static size_t s_total;      /* bytes ever written; head = s_total % s_size */
static vprintf_like_t s_orig_vprintf;
static portMUX_TYPE s_mux = portMUX_INITIALIZER_UNLOCKED;

static void ring_append(const char *s, size_t n)
{
    taskENTER_CRITICAL(&s_mux);
    for (size_t i = 0; i < n; i++) {
        s_ring[(s_total + i) % s_size] = s[i];
    }
    s_total += n;
    taskEXIT_CRITICAL(&s_mux);
}

static int log_hook(const char *fmt, va_list ap)
{
    char line[LINE_MAX_LEN];
    va_list copy;
    va_copy(copy, ap);
    int n = vsnprintf(line, sizeof(line), fmt, copy);
    va_end(copy);
    if (n > 0) {
        ring_append(line, (size_t)n < sizeof(line) ? (size_t)n : sizeof(line) - 1);
    }
    return s_orig_vprintf(fmt, ap);
}

esp_err_t diag_log_init(size_t ring_size)
{
    if (s_ring) {
        return ESP_OK;
    }
    s_ring = heap_caps_malloc(ring_size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!s_ring) {
        s_ring = malloc(ring_size);
    }
    if (!s_ring) {
        return ESP_ERR_NO_MEM;
    }
    s_size = ring_size;
    s_total = 0;
    s_orig_vprintf = esp_log_set_vprintf(log_hook);
    return ESP_OK;
}

size_t diag_log_read(char *dst, size_t dst_size)
{
    if (!s_ring || dst_size == 0) {
        if (dst_size) {
            dst[0] = '\0';
        }
        return 0;
    }
    taskENTER_CRITICAL(&s_mux);
    size_t avail = s_total < s_size ? s_total : s_size;
    size_t n = avail < dst_size - 1 ? avail : dst_size - 1;
    size_t start = s_total - n;
    for (size_t i = 0; i < n; i++) {
        dst[i] = s_ring[(start + i) % s_size];
    }
    bool clipped = start > 0;
    taskEXIT_CRITICAL(&s_mux);
    dst[n] = '\0';

    if (clipped) {
        /* drop the partial first line */
        char *nl = memchr(dst, '\n', n);
        if (nl) {
            size_t skip = (size_t)(nl - dst) + 1;
            memmove(dst, dst + skip, n - skip + 1);
            n -= skip;
        }
    }
    return n;
}

void diag_log_clear(void)
{
    taskENTER_CRITICAL(&s_mux);
    s_total = 0;
    taskEXIT_CRITICAL(&s_mux);
}
