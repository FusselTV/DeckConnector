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
 * Connections to the deckbridge servers (https://deckconnector.com/docs/own-server). The server the config
 * names in `host` is active while it is connected, else the first connected one; only the active one may
 * draw, the others wait as standby.
 */

/* All hooks are required. */
typedef struct {
    /* Write the whole `status` message. Returns its length, 0 when it does not fit. */
    size_t (*status_json)(char *buf, size_t cap);
    /* Version of the WLAN co-processor, or NULL. */
    const char *(*c6_version)(void);
    /* A server asked for an update. */
    void (*ota_requested)(const char *target, const char *url);
    /* The active server changed; -1 means none is connected. */
    void (*active_changed)(int index);
} db_link_hooks_t;

esp_err_t db_link_start(const db_link_hooks_t *hooks);

/* Tell the active server it may draw again, e.g. after the menu closed. It redraws everything on `role`. */
void db_link_resend_role(void);
/* The active server's last brightness on the deck again, when it starts to draw. */
void db_link_brightness_again(void);
/* Disabled, the bridge stays connected but every server is on standby. */
void db_link_set_enabled(bool enabled);
/* Take server URLs the config changed: those links connect again to the new address. */
void db_link_reload(void);

/* The deck the servers draw on: their protocol knows one, the first attached; slot 0 while none is. */
int db_link_deck(void);
/* Inputs for the active server dropped from a full send queue. */
uint32_t db_link_inputs_dropped(void);
bool db_link_connected(int index);
db_health_t db_link_health(int index);
/* Try again now instead of after the wait. */
void db_link_retry(int index);
const char *db_link_url(int index);

/* Send a JSON text message to every connected server. */
void db_link_broadcast(const char *json);
