#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* The names the board's own TLS certificate carries: "<name>.local,localhost" and each private IPv4 address
 * (network order, 0 for none), once. False when they do not fit. */
bool db_cert_names(const char *name, const uint32_t *ips, int n, char *out, size_t cap);

/* A certificate made for the names `have` and valid until `until` (Unix seconds) must be made again for the
 * names `want` at `now`: another name, a new address, or less than 30 days left. A lost address alone does
 * not count, so a cable pulled for a moment changes nothing. */
bool db_cert_renew(const char *have, const char *want, int64_t now, int64_t until);
