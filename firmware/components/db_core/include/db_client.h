#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* "1.2.3.4" and nothing else: no port, no spaces, no leading zeros. Network order in *out. */
bool db_ipv4(const char *s, size_t len, uint32_t *out);

/* The client behind a request (all network order): the last entry of X-Forwarded-For, the one the proxy
 * added, when the peer is the trusted proxy (0: none) and that entry is an IPv4 address; else the peer. */
uint32_t db_client_ip(uint32_t peer, uint32_t proxy, const char *forwarded_for);
