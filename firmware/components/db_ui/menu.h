#pragma once

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

#include "db_config.h"
#include "db_device.h"

#define UI_MAX_TILES 8
#define UI_SEGMENTS  4

typedef enum { TILE_NONE, TILE_CARD, TILE_BUTTON } ui_tile_kind_t;
typedef enum { BADGE_NONE, BADGE_OPEN, BADGE_CLOSE } ui_badge_t;

typedef struct {
    ui_tile_kind_t kind;
    const char *icon; /* ICON_* from icons.h */
    char label[24];
    char value[40]; /* cards only */
    char sub[40];
    uint32_t color; /* frame and icon colour while active, close badge */
    bool active;
    bool dim; /* a button that does nothing right now */
    ui_badge_t badge;
} ui_tile_t;

/* One quarter of the touch strip, over its encoder. `span` > 1 joins the next ones into one wide segment. */
typedef struct {
    uint8_t span; /* 0: part of the segment before */
    const char *icon;
    uint32_t icon_color;
    char label[24];
    char value[48];
    char sub[112];
    int bar; /* 0 to 100, < 0 for none */
} ui_segment_t;

typedef struct {
    ui_tile_t tiles[UI_MAX_TILES];
    ui_segment_t seg[UI_SEGMENTS];
} ui_screen_t;

typedef struct {
    bool app_client;
    bool companion_client;
    const char *c6;
} ui_state_t;

/* `changed` is called whenever the menu wants a redraw or closed itself. */
void ui_menu_init(void (*changed)(void));
bool ui_menu_open(void);
void ui_menu_open_main(void);
/* Input while the device UI draws: the menu, or the home screen when the menu is closed. True to redraw. */
bool ui_menu_input(const db_input_t *ev);
/* The open menu page, or the home screen. */
void ui_menu_screen(ui_screen_t *sc, const ui_state_t *st);
