/*
 * captive_dns - the DNS answer of the captive portal, without any socket code
 * (host-testable).
 *
 * Every A query is answered with the setup AP's own address, so that the
 * connectivity check of a phone or laptop ("generate_204", "hotspot-detect")
 * lands on our web server, which redirects it to the setup page. That makes
 * the operating system open the portal by itself.
 */
#pragma once

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define CAPTIVE_DNS_TTL_S 10

/*
 * Builds the reply to the DNS query `q` (`qlen` bytes) in `out`.
 *  - A/IN query       -> one answer: `ipv4` (network byte order, as in lwIP)
 *  - any other type   -> NOERROR without answer (e.g. AAAA: the client falls back to A)
 * Returns the reply length, or 0 if the packet is no single-question standard
 * query or `out` is too small (then nothing is sent).
 */
size_t captive_dns_reply(const uint8_t *q, size_t qlen, uint32_t ipv4, uint8_t *out, size_t out_size);

#ifdef __cplusplus
}
#endif
