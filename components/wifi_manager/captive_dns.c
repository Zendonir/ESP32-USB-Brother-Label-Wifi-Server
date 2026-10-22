#include "captive_dns.h"

#include <stdbool.h>
#include <string.h>

#define DNS_HEADER_LEN 12
#define DNS_TYPE_A     1
#define DNS_CLASS_IN   1

static uint16_t rd16(const uint8_t *p)
{
    return (uint16_t)((p[0] << 8) | p[1]);
}

static void wr16(uint8_t *p, uint16_t v)
{
    p[0] = (uint8_t)(v >> 8);
    p[1] = (uint8_t)v;
}

size_t captive_dns_reply(const uint8_t *q, size_t qlen, uint32_t ipv4, uint8_t *out, size_t out_size)
{
    if (qlen < DNS_HEADER_LEN) {
        return 0;
    }
    uint16_t flags = rd16(q + 2);
    /* Only queries (QR = 0) with opcode QUERY and exactly one question. */
    if ((flags & 0x8000) || ((flags >> 11) & 0x0f) != 0 || rd16(q + 4) != 1) {
        return 0;
    }
    /* QNAME: labels up to the root label; no compression pointers in a question. */
    size_t pos = DNS_HEADER_LEN;
    for (;;) {
        if (pos >= qlen) {
            return 0;
        }
        uint8_t len = q[pos];
        if (len == 0) {
            pos++;
            break;
        }
        if (len > 63) {
            return 0;
        }
        pos += 1 + (size_t)len;
    }
    if (pos + 4 > qlen) {
        return 0;
    }
    uint16_t qtype = rd16(q + pos);
    uint16_t qclass = rd16(q + pos + 2);
    size_t question_end = pos + 4;

    bool answer = qtype == DNS_TYPE_A && qclass == DNS_CLASS_IN;
    size_t need = question_end + (answer ? 16 : 0);
    if (need > out_size) {
        return 0;
    }
    /* Header and question are echoed; any additional records (EDNS) are dropped. */
    memcpy(out, q, question_end);
    /* QR = 1, opcode 0, AA = 1, RD copied, RA = 0, RCODE = NOERROR */
    wr16(out + 2, (uint16_t)(0x8000 | 0x0400 | (flags & 0x0100)));
    wr16(out + 6, answer ? 1 : 0);  /* ANCOUNT */
    wr16(out + 8, 0);               /* NSCOUNT */
    wr16(out + 10, 0);              /* ARCOUNT */
    if (!answer) {
        return question_end;
    }
    uint8_t *a = out + question_end;
    wr16(a, 0xC000 | DNS_HEADER_LEN); /* name: pointer to the question */
    wr16(a + 2, DNS_TYPE_A);
    wr16(a + 4, DNS_CLASS_IN);
    a[6] = 0;
    a[7] = 0;
    wr16(a + 8, CAPTIVE_DNS_TTL_S);
    wr16(a + 10, 4);
    memcpy(a + 12, &ipv4, 4);         /* already in network byte order */
    return need;
}
