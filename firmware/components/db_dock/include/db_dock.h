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
 * Several hosts may stay connected at once. Only the one matching the controller draws on the deck and gets
 * its input; the others draw into their kept picture, which shows as soon as they are picked.
 */

#define DB_DOCK_HOSTS 3

typedef struct {
    db_controller_t kind; /* DB_CTRL_SERVER while still unknown */
    char ip[16];
    bool device; /* also holds the device port */
} db_dock_host_t;

/* `clients_changed` tells which kinds of host hold the device port, after every change. */
esp_err_t db_dock_start(db_controller_t controller, void (*clients_changed)(bool app, bool companion));
void db_dock_set_controller(db_controller_t controller);
size_t db_dock_hosts(db_dock_host_t *out, size_t max);
