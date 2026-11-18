#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "db_link.h"
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
typedef struct {
    uint8_t count;    /* digits typed */
    bool wrong;       /* the last entry was wrong, for a moment */
    int16_t rotation; /* as Companion turns the surface: 0, 90, -90 or 180 */
} db_satellite_pin_t;

/* Companion locked `deck` with its PIN. */
bool db_satellite_locked(int deck, db_satellite_pin_t *pin);
/* One PIN digit for a locked `deck`, from a real press only. */
void db_satellite_pincode(int deck, int digit);
/* Companion's IPv4 address in network order while connected, else 0. */
uint32_t db_satellite_peer(void);
/* What the client connects to, "" when no address is set. */
const char *db_satellite_address(void);
