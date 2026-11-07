#pragma once

#include <stdbool.h>

#include "db_link.h"
#include "esp_err.h"

/*
 * Bitfocus Companion over its Satellite API (TCP 16622, docs/satellite.md): the bridge connects to the
 * address in the config `satellite` and offers the attached deck. Companion then draws on it like on a deck
 * over USB, with the same rows and columns, so its pages show unchanged.
 */

/* `changed` runs when Companion took the deck or let it go. */
esp_err_t db_satellite_start(void (*changed)(void));
bool db_satellite_connected(void);
db_health_t db_satellite_health(void);
void db_satellite_retry(void);
/* Connect to `addr` (as in the satellite setting) once and read Companion's greeting, within about 3 s each.
 * NULL and the version when it is Companion, else why not. */
const char *db_satellite_probe(const char *addr, const char *pin_hex, char *version, size_t cap);
/* Companion's IPv4 address in network order while connected, else 0. */
uint32_t db_satellite_peer(void);
/* What the client connects to, "" when no address is set. */
const char *db_satellite_address(void);
