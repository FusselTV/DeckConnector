#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "db_deck.h"
#include "esp_err.h"

/*
 * The on-device UI. It decides who may draw on the device: the menu and updates win, then the dock client or
 * the active server. Whenever nobody else draws, it shows what is missing (network, server, client).
 *
 * Menu: hold the two controls db_ui_menu_chord() names for `menu_hold_ms`, or call db_ui_open_menu().
 */

esp_err_t db_ui_start(void);

void db_ui_set_active_link(int index);
/* A Network Dock host came, left or told what it is. */
void db_ui_hosts_changed(void);
/* pct < 0 ends the update screen. */
void db_ui_set_update(int pct, const char *what);
void db_ui_set_c6_version(const char *version);
/* The source that draws once no menu or update covers it: the configured host, else the first of its kind. */
db_source_t db_ui_chosen_source(void);
/* Draw again and pick the writer again, e.g. after the controller changed. */
void db_ui_refresh(void);
/* What the deck shows as one JPEG, upright; the caller frees it with heap_caps_free. */
esp_err_t db_ui_snapshot(uint8_t **jpeg, size_t *len);
/* The two controls that open the menu (config `menu_chord`): index 0 and *last of the encoders or the keys.
 * False when there are none. */
bool db_ui_menu_chord(int encoders, int keys, bool *on_encoders, int *last);
/* True once for the token the welcome pages show as a QR code, while it is shown and not 10 min old. */
bool db_ui_login_take(const char *token);
/* The web wizard's step (PUT /api/setup), shown on the deck during setup. */
void db_ui_setup_step(int step);
/* ESP_ERR_INVALID_STATE when no deck is connected. */
esp_err_t db_ui_open_menu(void);
