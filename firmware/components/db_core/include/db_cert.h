#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* The names the board's own TLS certificate carries: "<name>.local,localhost". No IP address: the device CA
 * may only vouch for this board, and a stolen board must not be able to pass for another host on the LAN. */
bool db_cert_names(const char *name, char *out, size_t cap);

/* The device CA's name constraints (DER): only <name>.local and localhost, no IP address. Its length, 0 when
 * it does not fit. */
size_t db_cert_constraints(const char *name, uint8_t *out, size_t cap);

/* A certificate made for the names `have` and valid until `until` (Unix seconds) must be made again for the
 * names `want` at `now`: another name, or less than 30 days left. */
bool db_cert_renew(const char *have, const char *want, int64_t now, int64_t until);
