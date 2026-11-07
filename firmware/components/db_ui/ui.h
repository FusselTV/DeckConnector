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

typedef enum { TILE_NONE, TILE_CARD, TILE_BUTTON, TILE_QR } ui_tile_kind_t; /* TILE_QR: ui_screen_t.qr */
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

#define UI_QR_MAX 41 /* version 6: a WLAN name of 32 characters and a password of 63 still fit */

/* QR modules, row by row, one bit each, 1 = dark. */
typedef struct {
    uint8_t size; /* modules per side, 0: none */
    uint8_t bits[(UI_QR_MAX * UI_QR_MAX + 7) / 8];
} ui_qr_t;

typedef struct {
    ui_tile_t tiles[UI_MAX_KEYS];
    ui_segment_t seg[UI_SEGMENTS];
    ui_qr_t qr; /* one per screen; its tile carries what it encodes as value, so a change redraws it */
} ui_screen_t;

typedef struct {
    uint8_t cols;
    uint8_t rows;
    uint8_t encoders;
    bool strip;
    uint16_t key_px;
} ui_layout_t;

enum { UI_CTRL_SERVER, UI_CTRL_COMPANION, UI_CTRL_APP, UI_CTRL_COUNT }; /* the values of db_controller_t */

#define UI_SOURCES 8

enum { UI_VIA_SERVER, UI_VIA_SATELLITE, UI_VIA_DOCK }; /* the protocol a source speaks */

/* One connected host that can draw: Companion over Satellite, a Network Dock host, or a server. */
typedef struct {
    uint8_t kind;   /* UI_CTRL_*: the program */
    uint8_t via;    /* UI_VIA_* */
    bool chosen;    /* draws now, or would if nothing covered the deck */
    char label[40]; /* the host's name, or its address when it has none */
    char id[96];    /* what the config keeps: the dock host's IP, the server's URL, or "satellite" */
} ui_source_t;

typedef struct {
    int64_t now_us;
    int controller;               /* the program of the chosen host */
    uint8_t hosts[UI_CTRL_COUNT]; /* connected hosts per kind */
    ui_source_t sources[UI_SOURCES];
    uint8_t source_count;
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
    ui_qr_t ap_qr;    /* joins the setup WLAN when a phone scans it */
    char request[16]; /* a host that is not paired asks to connect */
    char request_name[40];
    char offer_name[32]; /* a Companion found by mDNS while no Satellite address is set */
    char offer_addr[24]; /* its "host:port" */
    char menu_chord[12];
    int menu_hold_ms;
    char server_url[96];
    char web_pass[24];
    char fw[32];
    char wlan_fw[16];
    int brightness;
    int update_pct; /* < 0: no update running */
    char update_what[24];
    bool setup_done;
    uint8_t setup_step; /* the web wizard's step, 0 when it does not run */
    char login_token[17]; /* one login for whoever scans login_qr; empty: none */
    ui_qr_t login_qr;
    bool picked; /* the config names a host or a program other than the default: someone chose */
} ui_model_t;

typedef enum {
    UI_DO_NOTHING,
    UI_DO_SOURCE,     /* value: index in ui_model_t.sources */
    UI_DO_ANSWER,     /* value: 1 lets the requesting host in */
    UI_DO_BRIGHTNESS, /* value: change in percent */
    UI_DO_AP_START,
    UI_DO_AP_STOP,
    UI_DO_RESTART,
    UI_DO_RESET,
    UI_DO_SAVE_LAN, /* dhcp, octets */
    UI_DO_SETUP_DONE,
    UI_DO_OFFER, /* value: 1 connects to the offered Companion over Satellite, 0 asks no more until a restart */
} ui_do_t;

typedef struct {
    ui_do_t what;
    int value;
    bool dhcp;
    uint8_t octets[4][4];
} ui_action_t;

typedef enum {
    PAGE_HOME,
    PAGE_MAIN,
    PAGE_LAN,
    PAGE_SETUP_AP,
    PAGE_WELCOME,
    PAGE_WELCOME_NET,
    PAGE_SOURCES, /* every connected host, when the top row has no room for all */
    PAGE_REQUEST, /* shown over every page while a host that is not paired asks */
    PAGE_OFFER,   /* instead of home or welcome while a found Companion is offered */
} ui_page_t;

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
    ui_page_t pick_back; /* PAGE_SOURCES goes back here */
    uint32_t keys_down;  /* pressed on this page */
    uint8_t encoders_down;
} ui_menu_t;

ui_layout_t ui_layout_of(const db_device_info_t *info);

/* The menu is open (anything but the home screen and the welcome pages). */
bool ui_menu_open(const ui_menu_t *m);
void ui_menu_open_main(ui_menu_t *m, int64_t now);
/* The menu controls for `chord` (auto, keys, encoders, off; anything else counts as auto): index 0 and *last of the
 * encoders or keys. False for off or when the deck has fewer than two. */
bool ui_menu_chord(const char *chord, int encoders, int keys, bool *on_encoders, int *last);
/* True when the screen changed; `act` says what the bridge has to do. Not reentrant. */
bool ui_menu_input(ui_menu_t *m, const ui_model_t *md, const ui_layout_t *l, const db_input_t *ev,
                   ui_action_t *act);
/* Once a second: closes an idle menu, ends a confirm, counts down a held restart key. True to redraw. */
bool ui_menu_tick(ui_menu_t *m, const ui_model_t *md);
void ui_menu_screen(const ui_menu_t *m, const ui_model_t *md, const ui_layout_t *l, ui_screen_t *sc);
/* Before setup is done the first host that connects draws without a tap: true and `act` when it is there. */
bool ui_first_source(const ui_model_t *md, ui_action_t *act);
void ui_update_screen(const ui_model_t *md, const ui_layout_t *l, ui_screen_t *sc);

/*
 * Draw `sc` onto `c` for `dev`. With `shown` only the keys and strip parts that differ from it are drawn;
 * NULL draws everything. Only key and strip areas are drawn: the caller clears the gaps once, when it
 * allocates the canvas. Returns how many areas changed and puts them in `dirty`.
 */
int ui_render(ui_canvas_t *c, const db_device_info_t *dev, const ui_screen_t *sc, const ui_screen_t *shown,
              db_rect_t *dirty, int max);
