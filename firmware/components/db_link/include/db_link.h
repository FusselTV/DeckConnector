#pragma once

#include <stdbool.h>
#include <stddef.h>

#include "esp_err.h"

/*
 * Connections to the deckbridge servers (docs/protocol.md). The first connected server is active and may
 * draw; the other one waits as standby.
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

/* Index of the active server, or -1. */
int db_link_active(void);
bool db_link_connected(int index);
const char *db_link_url(int index);

/* Send a JSON text message to every connected server. */
void db_link_broadcast(const char *json);
