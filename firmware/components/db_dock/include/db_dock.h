#pragma once

#include <stdbool.h>
#include <stddef.h>

#include "db_config.h"
#include "esp_err.h"

/*
 * Network Dock emulation. Bitfocus Companion and the Elgato Stream Deck app find the bridge over mDNS
 * (_elg._tcp) and drive the attached Stream Deck as if it hung on an Elgato Network Dock. The protocol
 * ("Cora") is the one of @elgato-stream-deck/tcp; see docs/dock.md.
 *
 * Several hosts may stay connected at once, each with its own deck source. Only the writer draws on the deck
 * and gets its input; the others draw into their kept picture, which shows as soon as they are picked.
 */

#define DB_DOCK_HOSTS 4

typedef struct {
    db_controller_t kind; /* DB_CTRL_SERVER while still unknown */
    char ip[16];
    int source; /* the deck source of its device port, -1 while it has none */
} db_dock_host_t;

/* `hosts_changed` runs after a host took or left a device port, or told what it is. */
esp_err_t db_dock_start(void (*hosts_changed)(void));
size_t db_dock_hosts(db_dock_host_t *out, size_t max);

/* A host that is not paired asked to connect in the last minute: its address. */
bool db_dock_request(char ip[16]);
/* Let the requesting host in from now on, or not. */
void db_dock_answer(bool accept);
/* New hosts get in and are remembered for `seconds`; 0 ends it, also the minutes after a boot. */
void db_dock_pairing(int seconds);
int db_dock_pairing_left(void);
size_t db_dock_known(char out[][16], size_t max);
/* NULL forgets every paired host. */
void db_dock_forget(const char *ip);
