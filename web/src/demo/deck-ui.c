/* The deck's own screen in the demo: the firmware's db_ui code, built to WebAssembly by scripts/deck-ui.mjs
 * and driven by deck-ui.ts. JS fills the model field by field (ui_model_t names), then asks for input, a
 * screen or its pixels. */
#include <stddef.h>
#include <stdlib.h>
#include <string.h>

#include "db_driver.h"
#include "db_tones.h"
#include "ui.h"

#define EXPORT(name) __attribute__((export_name(#name)))

static db_err_t no_write(const db_hid_io_t *io, const uint8_t *r, size_t len)
{
    (void)io;
    (void)r;
    (void)len;
    return DB_OK;
}

static db_err_t no_flush(const db_hid_io_t *io)
{
    (void)io;
    return DB_OK;
}

static db_err_t feature(const db_hid_io_t *io, uint8_t *buf, size_t len, size_t *out_len)
{
    (void)io;
    memset(buf + 1, 0, len - 1);
    *out_len = len;
    return DB_OK;
}

static db_err_t set_feature(const db_hid_io_t *io, const uint8_t *buf, size_t len)
{
    (void)io;
    (void)buf;
    (void)len;
    return DB_OK;
}

static const db_hid_io_t io = {no_write, no_flush, feature, set_feature, 1024};
static db_dev_t dev = {.io = &io};
static ui_layout_t layout;
static ui_model_t md;
static ui_menu_t menu = {.confirm_key = -1};
static ui_screen_t sc;
static ui_action_t act;
static ui_canvas_t canvas;
static char buf[256];

enum { T_STR, T_INT, T_BOOL };
#define F(f, t) {#f, offsetof(ui_model_t, f), sizeof(((ui_model_t *)0)->f), t}
static const struct {
    const char *name;
    size_t off;
    size_t size;
    int type;
} fields[] = {
    F(controller, T_INT),  F(lan_link, T_BOOL),  F(lan_up, T_BOOL),    F(lan_dhcp, T_BOOL),
    F(lan_ip, T_STR),      F(wlan_chip, T_BOOL), F(wlan_up, T_BOOL),   F(wlan_rssi, T_INT),
    F(wlan_ip, T_STR),     F(wifi_ssid, T_STR),  F(ap_on, T_BOOL),     F(ap_ssid, T_STR),
    F(ap_pass, T_STR),     F(ap_ip, T_STR),      F(request, T_STR),    F(request_name, T_STR),
    F(offer_name, T_STR),  F(offer_addr, T_STR), F(menu_chord, T_STR), F(menu_hold_ms, T_INT),
    F(server_url, T_STR),  F(web_pass, T_STR),   F(fw, T_STR),
    F(wlan_fw, T_STR),     F(brightness, T_INT), F(update_pct, T_INT), F(update_wlan, T_BOOL),
    F(setup_done, T_BOOL), F(setup_step, T_INT), F(picked, T_BOOL),    F(show_pass, T_BOOL),
    F(reset_ask, T_BOOL),  F(login_ip, T_STR),   F(login_who, T_STR),  F(login_mac, T_STR),
    F(login_name, T_STR),  F(satellite, T_STR),  F(english, T_BOOL), F(sat_failed, T_BOOL),
};

EXPORT(deck_init) int deck_init(void)
{
    dev.driver = db_driver_find(0x0fd9, 0x0084); /* the Stream Deck + of the demo */
    if (!dev.driver || dev.driver->open(&dev, 0x0fd9, 0x0084) != DB_OK) {
        return 0;
    }
    layout = ui_layout_of(&dev.info);
    canvas = (ui_canvas_t){dev.info.displays[0].width, dev.info.displays[0].height, NULL};
    canvas.px = calloc((size_t)canvas.w * canvas.h, 3);
    return canvas.px != NULL;
}

EXPORT(deck_buf) char *deck_buf(void)
{
    return buf;
}

EXPORT(deck_begin) void deck_begin(double now_ms)
{
    md = (ui_model_t){.now_us = (int64_t)(now_ms * 1000)};
}

/* buf holds "name\0value\0". False for a name ui_model_t does not have. */
EXPORT(deck_set) int deck_set(void)
{
    const char *v = buf + strlen(buf) + 1;
    for (size_t i = 0; i < sizeof(fields) / sizeof(fields[0]); i++) {
        if (strcmp(fields[i].name, buf) != 0) {
            continue;
        }
        char *at = (char *)&md + fields[i].off;
        if (fields[i].type == T_STR) {
            strncpy(at, v, fields[i].size - 1);
        } else if (fields[i].type == T_BOOL) {
            *(bool *)at = v[0] == '1';
        } else if (fields[i].size == 1) {
            *(uint8_t *)at = (uint8_t)atoi(v);
        } else {
            *(int *)at = atoi(v);
        }
        return 1;
    }
    return 0;
}

/* buf holds "label\0id\0". */
EXPORT(deck_source) void deck_source(int kind, int via, int chosen)
{
    if (md.source_count >= UI_SOURCES || kind < 0 || kind >= UI_CTRL_COUNT) {
        return;
    }
    ui_source_t *s = &md.sources[md.source_count++];
    *s = (ui_source_t){.kind = (uint8_t)kind, .via = (uint8_t)via, .chosen = chosen};
    strncpy(s->label, buf, sizeof(s->label) - 1);
    strncpy(s->id, buf + strlen(buf) + 1, sizeof(s->id) - 1);
    md.hosts[kind]++;
}

EXPORT(deck_lan_cfg) void deck_lan_cfg(int field, int a, int b, int c, int d)
{
    uint8_t *o = md.lan_cfg[field & 3];
    o[0] = (uint8_t)a;
    o[1] = (uint8_t)b;
    o[2] = (uint8_t)c;
    o[3] = (uint8_t)d;
}

/* The menu's state, which the demo keeps per bridge and puts back before each call. */
EXPORT(deck_menu) ui_menu_t *deck_menu(void)
{
    return &menu;
}

EXPORT(deck_menu_size) int deck_menu_size(void)
{
    return sizeof(menu);
}

EXPORT(deck_menu_reset) void deck_menu_reset(void)
{
    menu = (ui_menu_t){.confirm_key = -1};
}

EXPORT(deck_open) void deck_open(void)
{
    if (!ui_menu_open(&menu)) {
        ui_menu_open_main(&menu, md.now_us);
    }
}

EXPORT(deck_close) void deck_close(void)
{
    ui_menu_close(&menu, md.now_us);
}

EXPORT(deck_is_open) int deck_is_open(void)
{
    return ui_menu_open(&menu);
}

EXPORT(deck_tick) int deck_tick(void)
{
    return ui_menu_tick(&menu, &md);
}

/* The action's kind (ui_do_t); deck_act_value has its value. */
EXPORT(deck_input) int deck_input(int kind, int index, int value, int x, int y, int x2, int from_deck)
{
    db_input_t ev = {
        .kind = (db_input_kind_t)kind,
        .index = (uint8_t)index,
        .value = (int16_t)value,
        .x = (uint16_t)x,
        .y = (uint16_t)y,
        .x2 = (uint16_t)x2,
        .y2 = (uint16_t)y,
        .t_us = (uint64_t)md.now_us,
    };
    md.from_deck = from_deck;
    act = (ui_action_t){0};
    ui_menu_input(&menu, &md, &layout, &ev, &act);
    return act.what;
}

EXPORT(deck_act_value) int deck_act_value(void)
{
    return act.value;
}

/* Builds the screen and returns a hash of it, so equal screens are painted once. */
EXPORT(deck_screen) uint32_t deck_screen(int update)
{
    memset(&sc, 0, sizeof(sc));
    if (update) {
        ui_update_screen(&md, &layout, &sc);
    } else {
        ui_menu_screen(&menu, &md, &layout, &sc);
    }
    uint32_t h = 2166136261u;
    for (size_t i = 0; i < sizeof(sc); i++) {
        h = (h ^ ((const uint8_t *)&sc)[i]) * 16777619u;
    }
    return h;
}

/* The last screen as B, G, R bytes, canvas.w by canvas.h. */
EXPORT(deck_render) uint8_t *deck_render(void)
{
    memset(canvas.px, 0, (size_t)canvas.w * canvas.h * 3);
    db_rect_t dirty[DB_MAX_TARGETS + UI_SEGMENTS];
    ui_render(&canvas, &dev.info, &sc, NULL, dirty, DB_MAX_TARGETS + UI_SEGMENTS);
    return canvas.px;
}

static int16_t pcm[16000 * 3];

/* The sound named in the buffer at `volume`, as db_audio plays it: 16 kHz samples at deck_pcm; how many. 0 for
 * an unknown name. */
EXPORT(deck_tone) size_t deck_tone(int volume)
{
    for (int i = 0; i < DB_SOUND_COUNT; i++) {
        if (strcmp(buf, db_sound_name((db_sound_t)i)) == 0) {
            return db_tones_render((db_sound_t)i, volume, 16000, pcm, sizeof(pcm) / sizeof(pcm[0]));
        }
    }
    return 0;
}

EXPORT(deck_pcm) int16_t *deck_pcm(void)
{
    return pcm;
}
