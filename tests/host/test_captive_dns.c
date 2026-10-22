/*
 * Host unit tests for the captive portal DNS answer (captive_dns.c). The
 * queries are built by hand after RFC 1035 section 4.1.
 */
#include <stdio.h>
#include <string.h>

#include "captive_dns.h"

static int g_fail;
#define CHECK(c) do { if (!(c)) { fprintf(stderr, "%s:%d: CHECK failed: %s\n", __FILE__, __LINE__, #c); g_fail++; } } while (0)

/* Query for `name` (dotted) with type/class; `extra` appends an EDNS OPT record. */
static size_t query(uint8_t *q, const char *name, uint16_t type, uint16_t cls, int extra)
{
    size_t n = 0;
    const uint8_t hdr[12] = { 0x12, 0x34, 0x01, 0x00, 0, 1, 0, 0, 0, 0, 0, (uint8_t)(extra ? 1 : 0) };
    memcpy(q, hdr, sizeof(hdr));
    n = sizeof(hdr);
    const char *p = name;
    while (*p) {
        const char *dot = strchr(p, '.');
        size_t l = dot ? (size_t)(dot - p) : strlen(p);
        q[n++] = (uint8_t)l;
        memcpy(q + n, p, l);
        n += l;
        p += l + (dot ? 1 : 0);
    }
    q[n++] = 0;
    q[n++] = (uint8_t)(type >> 8); q[n++] = (uint8_t)type;
    q[n++] = (uint8_t)(cls >> 8);  q[n++] = (uint8_t)cls;
    if (extra) {
        const uint8_t opt[11] = { 0, 0, 41, 0x10, 0, 0, 0, 0, 0, 0, 0 };
        memcpy(q + n, opt, sizeof(opt));
        n += sizeof(opt);
    }
    return n;
}

int main(void)
{
    uint8_t q[512], r[512];
    const uint8_t ip_bytes[4] = { 192, 168, 4, 1 };
    uint32_t ip;
    memcpy(&ip, ip_bytes, 4); /* network byte order, as lwIP stores it */

    /* A query of an Android connectivity check -> answer 192.168.4.1 */
    size_t ql = query(q, "connectivitycheck.gstatic.com", 1, 1, 0);
    size_t rl = captive_dns_reply(q, ql, ip, r, sizeof(r));
    CHECK(rl == ql + 16);
    CHECK(r[0] == 0x12 && r[1] == 0x34);              /* same id */
    CHECK(r[2] == 0x85 && r[3] == 0x00);              /* QR, AA, RD copied, NOERROR */
    CHECK(r[4] == 0 && r[5] == 1 && r[6] == 0 && r[7] == 1);
    CHECK(memcmp(r + 12, q + 12, ql - 12) == 0);      /* question echoed */
    CHECK(r[ql] == 0xC0 && r[ql + 1] == 12);          /* name pointer */
    CHECK(r[ql + 3] == 1 && r[ql + 5] == 1);          /* type A, class IN */
    CHECK(r[ql + 9] == CAPTIVE_DNS_TTL_S && r[ql + 11] == 4);
    CHECK(memcmp(r + ql + 12, ip_bytes, 4) == 0);

    /* EDNS additional record is dropped, the answer still follows the question */
    size_t ql2 = query(q, "captive.apple.com", 1, 1, 1);
    rl = captive_dns_reply(q, ql2, ip, r, sizeof(r));
    CHECK(rl == ql2 - 11 + 16);
    CHECK(r[10] == 0 && r[11] == 0);
    CHECK(memcmp(r + rl - 4, ip_bytes, 4) == 0);

    /* AAAA -> NOERROR without answer */
    ql = query(q, "www.msftconnecttest.com", 28, 1, 0);
    rl = captive_dns_reply(q, ql, ip, r, sizeof(r));
    CHECK(rl == ql && r[7] == 0 && (r[3] & 0x0f) == 0);

    /* Rejected: a response, two questions, truncated name, label too long, short buffer */
    ql = query(q, "a.b", 1, 1, 0);
    q[2] |= 0x80;
    CHECK(captive_dns_reply(q, ql, ip, r, sizeof(r)) == 0);
    ql = query(q, "a.b", 1, 1, 0);
    q[5] = 2;
    CHECK(captive_dns_reply(q, ql, ip, r, sizeof(r)) == 0);
    ql = query(q, "example.com", 1, 1, 0);
    CHECK(captive_dns_reply(q, 16, ip, r, sizeof(r)) == 0);
    CHECK(captive_dns_reply(q, ql - 2, ip, r, sizeof(r)) == 0);
    q[12] = 64;
    CHECK(captive_dns_reply(q, ql, ip, r, sizeof(r)) == 0);
    ql = query(q, "example.com", 1, 1, 0);
    CHECK(captive_dns_reply(q, ql, ip, r, ql + 15) == 0);
    CHECK(captive_dns_reply(q, 11, ip, r, sizeof(r)) == 0);

    if (g_fail) {
        fprintf(stderr, "%d check(s) failed\n", g_fail);
        return 1;
    }
    printf("captive dns tests passed\n");
    return 0;
}
