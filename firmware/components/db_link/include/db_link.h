#pragma once

#include <stdbool.h>
#include <stddef.h>

#include "esp_err.h"
#include "esp_timer.h"

#include <stdint.h>
#include <string.h>

/* How a connection the bridge makes is doing, for the status: the last error ("" while connected), since when
 * that holds, and the interface. Written by its task, copied whole by readers; a torn read shows a mixed
 * error text at worst. */
typedef struct {
    char error[40];
    int64_t since_us;
    bool over_wlan;
} db_health_t;

static inline void db_health_fail(db_health_t *h, const char *why)
{
    if (strcmp(h->error, why) != 0) {
        strlcpy(h->error, why, sizeof(h->error));
        h->since_us = esp_timer_get_time();
    }
}

static inline void db_health_ok(db_health_t *h, bool over_wlan)
{
    h->error[0] = '\0';
    h->over_wlan = over_wlan;
    h->since_us = esp_timer_get_time();
}

/*
 * Connections to surface hosts (https://deckconnector.com/docs/own-server), at most DB_SURFACE_LINKS. A
 * paired host becomes the active one when it asks with ROLE; only the active one draws, on every deck.
 */

/* All hooks are required. */
typedef struct {
    /* The active host changed; -1 means none draws. */
    void (*active_changed)(int index);
    /* The host on `link` waits for the admin to confirm `code`; NULL when it no longer does. */
    void (*pair_code)(int link, const char *code);
} db_link_hooks_t;

esp_err_t db_link_start(const db_link_hooks_t *hooks);

/* The active host's last brightness on `deck` again, when it starts to draw there. */
void db_link_brightness_again(int deck);
/* Disabled, the bridge stays connected but no host is active. */
void db_link_set_enabled(bool enabled);
/* Take server URLs and pins the config changed: those links connect again. */
void db_link_reload(void);

/* The first attached deck, slot 0 while none is: where the status lists the hosts. */
int db_link_deck(void);
/* The ERRORs sent to hosts: the code of entry `i` and in `n` how often; NULL past the last. */
const char *db_link_refusals(int i, uint32_t *n);
/* Inputs for the active host dropped from a full send queue. */
uint32_t db_link_inputs_dropped(void);
bool db_link_connected(int index);
db_health_t db_link_health(int index);
/* Try again now instead of after the wait. */
void db_link_retry(int index);
/* "" for an index without a link. */
const char *db_link_url(int index);
/* The code the decks show for a connected host that is not paired yet, else "". */
const char *db_link_pair_code(int index);
/* TCP_NODELAY of the link's socket as the stack reports it: 1, 0, or -1 while not connected. */
int db_link_nodelay(int index);
