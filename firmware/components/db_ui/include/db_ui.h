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
/* The deck shows a one-time login code, the password or the setup WLAN's password: pictures of the deck are then
 * for a password session only. */
bool db_ui_login_shown(void);
/* The web wizard's step (PUT /api/setup), shown on the deck during setup. */
void db_ui_setup_step(int step);
/* ESP_ERR_INVALID_STATE when no deck is connected. */
esp_err_t db_ui_open_menu(void);
/* Back to the home page, like the menu's close key; also hides the password page, cancels the reset question and
 * denies a login request. The deck goes back to who draws. */
void db_ui_close_menu(void);
/* The menu, the password page, the reset question or a login request covers the deck. */
bool db_ui_menu_open(void);
/* The deck shows the web login for 30 s. The number of decks it shows on, 0 when none is connected. */
int db_ui_show_password(void);
/* The deck asks for 60 s whether to reset every setting; only taps on the deck itself answer. Seconds left, -1
 * when no deck is connected. */
int db_ui_ask_reset(void);
void db_ui_cancel_reset(void);
/* The deck asks for `seconds` whether the browser at `ip` (network order) may log in; a newer call replaces the
 * question. False when no deck is connected. Only taps on the deck itself answer. */
bool db_ui_ask_login(uint32_t ip, const char *who, int seconds);
/* 1 accepted, 0 denied, -1 not answered (yet) for the last question. */
int db_ui_login_answer(void);
void db_ui_end_login(void);
