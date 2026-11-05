#pragma once

/*
 * The device UI without the chip: what the pages show (ui_model_t in, ui_screen_t out), how input moves
 * through them (ui_action_t out), and how a screen is drawn for a given deck. db_ui.c fills the model and
 * carries out the actions; host_test/test_ui.c renders every page for every deck.
 */

#include <stdbool.h>
#include <stdint.h>

#include "canvas.h"
#include "db_device.h"

#define UI_MAX_KEYS 32
#define UI_SEGMENTS 4

typedef enum { TILE_NONE, TILE_CARD, TILE_BUTTON } ui_tile_kind_t;
typedef enum { BADGE_NONE, BADGE_OPEN, BADGE_CLOSE } ui_badge_t;

/* A card only shows something; a button does something. */
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
    char label[32];
    char value[48];
    char sub[112];
    int bar; /* 0 to 100, < 0 for none */
} ui_segment_t;

typedef struct {
    ui_tile_t tiles[UI_MAX_KEYS];
    ui_segment_t seg[UI_SEGMENTS];
} ui_screen_t;

typedef struct {
    uint8_t cols;
    uint8_t rows;
    uint8_t encoders;
    bool strip;
    uint16_t key_px;
} ui_layout_t;

enum { UI_CTRL_SERVER, UI_CTRL_COMPANION, UI_CTRL_APP, UI_CTRL_COUNT }; /* the values of db_controller_t */

typedef struct {
    int64_t now_us;
    int controller;
    bool host_up[UI_CTRL_COUNT];
    bool lan_link;
    bool lan_up;
    bool lan_dhcp;
    char lan_ip[16];
    uint8_t lan_cfg[4][4]; /* configured IP, mask, gateway, DNS; the editor starts from these */
    bool wlan_chip;
    bool wlan_up;
    int wlan_rssi;
    char wlan_ip[16];
    char wifi_ssid[33];
    bool ap_on;
    char ap_ssid[33];
    char ap_pass[16];
    char ap_ip[16];
    char server_url[96];
    char web_pass[24];
    char fw[32];
    char wlan_fw[16];
    int brightness;
    int update_pct; /* < 0: no update running */
    char update_what[24];
    bool setup_done;
} ui_model_t;

typedef enum {
    UI_DO_NOTHING,
    UI_DO_CONTROLLER, /* value: UI_CTRL_* */
    UI_DO_BRIGHTNESS, /* value: change in percent */
    UI_DO_AP_START,
    UI_DO_AP_STOP,
    UI_DO_RESTART,
    UI_DO_RESET,
    UI_DO_SAVE_LAN, /* dhcp, octets */
    UI_DO_SETUP_DONE,
} ui_do_t;

typedef struct {
    ui_do_t what;
    int value;
    bool dhcp;
    uint8_t octets[4][4];
} ui_action_t;

typedef enum { PAGE_HOME, PAGE_MAIN, PAGE_LAN, PAGE_SETUP_AP, PAGE_WELCOME, PAGE_WELCOME_NET } ui_page_t;

typedef struct {
    ui_page_t page;
    int64_t last_input;
    int64_t restart_down;
    int64_t confirm_until;
    int confirm_key; /* the key waiting for its second tap */
    char note[64];
    bool lan_dhcp;
    int field;
    uint8_t octets[4][4];
    uint32_t keys_down; /* pressed on this page */
    uint8_t encoders_down;
} ui_menu_t;

ui_layout_t ui_layout_of(const db_device_info_t *info);

/* The menu is open (anything but the home screen and the welcome pages). */
bool ui_menu_open(const ui_menu_t *m);
void ui_menu_open_main(ui_menu_t *m, int64_t now);
/* True when the screen changed; `act` says what the bridge has to do. Not reentrant. */
bool ui_menu_input(ui_menu_t *m, const ui_model_t *md, const ui_layout_t *l, const db_input_t *ev,
                   ui_action_t *act);
/* Once a second: closes an idle menu, ends a confirm, counts down a held restart key. True to redraw. */
bool ui_menu_tick(ui_menu_t *m, const ui_model_t *md);
void ui_menu_screen(const ui_menu_t *m, const ui_model_t *md, const ui_layout_t *l, ui_screen_t *sc);
void ui_update_screen(const ui_model_t *md, const ui_layout_t *l, ui_screen_t *sc);

/*
 * Draw `sc` onto `c` for `dev`. With `shown` only the keys and strip parts that differ from it are drawn;
 * NULL draws everything. Returns how many areas changed and puts them in `dirty`.
 */
int ui_render(ui_canvas_t *c, const db_device_info_t *dev, const ui_screen_t *sc, const ui_screen_t *shown,
              db_rect_t *dirty, int max);
