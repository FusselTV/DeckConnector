#pragma once

#include <stdbool.h>

#include "db_link.h"
#include "db_sat.h"
#include "esp_err.h"

/*
 * Bitfocus Companion over its Satellite API (TCP 16622, https://deckconnector.com/docs/companion): the bridge
 * connects to the address in the config `satellite` and offers every attached deck, in slot order. Companion
 * then draws on it like on a deck over USB, with the same rows and columns, so its pages show unchanged.
 */

/* `changed` runs when a session began and when Companion took a deck or let it go. */
esp_err_t db_satellite_start(void (*changed)(void));
/* Companion has `deck`, or any deck for -1. */
bool db_satellite_connected(int deck);
db_health_t db_satellite_health(void);
void db_satellite_retry(void);
/* The address or pin was saved: the session ends, also when two saves in a row bring back the same text. */
void db_satellite_moved(void);
/* Connect to `addr` (as in the satellite setting) once and read Companion's greeting, within about 3 s each.
 * NULL and the version when it is Companion, else why not. */
const char *db_satellite_probe(const char *addr, const char *pin_hex, char *version, size_t cap);
/* The address `satellite` names, as last resolved, in network order; 0 before that. Behind a proxy it is the
 * proxy's. */
uint32_t db_satellite_resolved(void);
/* Companion locked `deck` with its PIN. */
bool db_satellite_locked(int deck, db_sat_pin_t *out);
/* One PIN digit for a locked `deck`. `real` and `only_ip` as for db_deck_simulate: without them it goes
 * nowhere. False when the digit was not sent. */
bool db_satellite_pincode(int deck, int digit, bool real, uint32_t only_ip);
/* Companion's IPv4 address in network order while connected, else 0. */
uint32_t db_satellite_peer(void);
/* What the client connects to, "" when no address is set. */
const char *db_satellite_address(void);
