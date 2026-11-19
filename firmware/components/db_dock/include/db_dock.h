#pragma once

#include <stdbool.h>
#include <stddef.h>

#include "db_config.h"
#include "esp_err.h"

/*
 * Network Dock emulation. Bitfocus Companion and the Elgato App find the bridge over mDNS
 * (_elg._tcp) and drive the attached deck as if it hung on an Elgato Network Dock. The protocol
 * ("Cora") is the one of @elgato-stream-deck/tcp: a 16 byte header (43 93 8a 41, flags, HID op, message id,
 * length) and a HID report, built in db_core/db_cora.c. Each deck is a dock of its own: deck k (its slot) has
 * the dock on port 5343 + 2k and the device on 5344 + 2k, whose reports pass through. Only announced while a
 * deck is plugged in: the Elgato App turns away a dock without a deck and does not look again until it
 * restarts. Companion asks the dock with GET_REPORT, the Elgato App with a plain write padded to 1024 bytes,
 * and only the Elgato App wants an ack (flag 0x4000) for every write.
 *
 * Several hosts may stay connected at once, each with its own deck source. Only the writer draws on the deck
 * and gets its input; the others draw into their kept picture, which shows as soon as they are picked.
 */

#define DB_DOCK_HOSTS 4 /* per deck */

typedef struct {
    db_controller_t kind; /* DB_CTRL_SERVER while still unknown */
    char ip[16];
    int deck;
    int source; /* the deck source of its device port, -1 while it has none */
} db_dock_host_t;

/* `hosts_changed` runs after a host took or left a device port, or told what it is. */
esp_err_t db_dock_start(void (*hosts_changed)(void));
/* The dock hosts of `deck`, or of every deck for -1. */
size_t db_dock_hosts(int deck, db_dock_host_t *out, size_t max);

/* A host that is not paired asked to connect in the last minute: its address. */
bool db_dock_request(char ip[16]);
/* Let the requesting host in from now on, or not. */
esp_err_t db_dock_answer(bool accept); /* an error: in RAM only, gone after a restart */
/* Hosts told "no" since the start, newest last; `at_us` is esp_timer time. A refused host asks again a minute
 * later. */
typedef struct {
    uint32_t ip;
    int64_t at_us;
} db_dock_denied_t;
size_t db_dock_denied(db_dock_denied_t *out, size_t max);
bool db_dock_undeny(uint32_t ip);
/* New hosts get in and are remembered for `seconds`; 0 ends it. */
void db_dock_pairing(int seconds);
int db_dock_pairing_left(void);
typedef struct {
    uint32_t ip;    /* the last address it came from */
    uint8_t mac[6]; /* zero when it was not on the same network */
    uint8_t kind;   /* db_controller_t; DB_CTRL_SERVER until it said what it is */
} db_dock_peer_t;

/* The hosts that connected (dock_paired_only off) or were paired (on), oldest first. */
size_t db_dock_known(db_dock_peer_t *out, size_t max);
bool db_dock_peer_connected(const db_dock_peer_t *p);
/* The name its router knows it by, "" until the lookup answered or when there is none. `ip` in network order.
 */
void db_dock_name(uint32_t ip, char name[40]);
/* From the ARP table: a host that just connected is in it, one behind a router is not and gets zeros. */
void db_dock_mac(uint32_t ip, uint8_t mac[6]);
/* Ends Companion's dock connections from the address the `satellite` setting names or Satellite is connected
 * to, and refuses them while it does: Companion would show the deck twice. */
void db_dock_drop_satellite_host(void);
/* NULL forgets every paired host. */
esp_err_t db_dock_forget(const char *ip);
