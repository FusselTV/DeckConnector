#pragma once

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

#include "db_device.h"

#define UI_MAX_TILES 8

typedef struct {
    char label[24];
    char sub[40];
    uint32_t color;
} ui_tile_t;

/* What one screen shows: a banner on the touch strip and one tile per key. */
typedef struct {
    char title[48];
    char line[96];
    uint32_t color;
    int progress; /* < 0: no progress bar */
    ui_tile_t tiles[UI_MAX_TILES];
} ui_screen_t;

static inline void ui_set_tile(ui_screen_t *sc, int i, const char *label, const char *sub, uint32_t color)
{
    if (i < UI_MAX_TILES) {
        snprintf(sc->tiles[i].label, sizeof(sc->tiles[i].label), "%s", label);
        snprintf(sc->tiles[i].sub, sizeof(sc->tiles[i].sub), "%s", sub ? sub : "");
        sc->tiles[i].color = color;
    }
}

/* `changed` is called whenever the menu wants a redraw or closed itself. */
void ui_menu_init(void (*changed)(void));
bool ui_menu_open(void);
void ui_menu_open_main(void);
/* Returns true when the screen changed. */
bool ui_menu_input(const db_input_t *ev);
void ui_menu_screen(ui_screen_t *sc);
