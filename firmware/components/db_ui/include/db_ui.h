#pragma once

#include <stdbool.h>

#include "esp_err.h"

/*
 * The on-device UI. It decides who may draw on the device: the menu and updates win, then the dock client or
 * the active server. Whenever nobody else draws, it shows what is missing (network, server, client).
 *
 * Menu: hold the first and the last encoder for 2 seconds (key 1 and the last key on decks without encoders).
 */

esp_err_t db_ui_start(void);

void db_ui_set_active_link(int index);
void db_ui_set_dock_client(bool connected);
/* pct < 0 ends the update screen. */
void db_ui_set_update(int pct, const char *what);
void db_ui_set_c6_version(const char *version);
