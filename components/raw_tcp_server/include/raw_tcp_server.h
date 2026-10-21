/*
 * raw_tcp_server - "RAW" / JetDirect / AppSocket print server (TCP port 9100).
 *
 * Every connection is one print job: everything received until the client
 * closes the connection is spooled and handed to the print queue unchanged.
 * A connection that breaks off or stays idle too long is discarded (nothing
 * is printed). There is no authentication on this protocol; access can be
 * limited to one network (CIDR) with raw_tcp_server_set_allowed().
 */
#pragma once

#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

esp_err_t raw_tcp_server_start(void);

/* "any" or "a.b.c.d/nn" (also a single address); stored in NVS. */
esp_err_t raw_tcp_server_set_allowed(const char *cidr);
void raw_tcp_server_get_allowed(char *buf, size_t size);

uint32_t raw_tcp_server_connections(void);

#ifdef __cplusplus
}
#endif
