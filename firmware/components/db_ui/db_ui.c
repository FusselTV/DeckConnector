#include "db_ui.h"

#include <stdio.h>
#include <string.h>

#include "canvas.h"
#include "db_config.h"
#include "db_deck.h"
#include "db_link.h"
#include "db_net.h"
#include "esp_app_desc.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "jpeg_port.h"
#include "menu.h"

#define QUALITY      80
#define REDRAW_DELAY 80 /* ms: collect bursts of changes into one image */
#define MENU_HOLD_US 2000000
#define C_BG         0x101418
#define C_TILE       0x232a31
#define C_OK         0x1f7a45
#define C_BAD        0x8c2a2a
#define C_WAIT       0x7a5a12
#define C_TEXT       0xffffff
#define C_DIM        0xb8c0c8

static const char *TAG = "ui";

static struct {
    TaskHandle_t task;
    int active_link;
    bool dock_client;
    int update_pct;
    char update_what[24];
    char c6[16];
    db_device_info_t dev;
    bool present;
    ui_canvas_t canvas;
    uint32_t seq;
    bool enc_down[8];
    bool key_down[32];
    int64_t chord_since;
} s = {.active_link = -1, .update_pct = -1};

/* ---------- who draws ---------- */

static db_source_t wanted_writer(void)
{
    if (ui_menu_open() || s.update_pct >= 0) {
        return DB_SOURCE_UI;
    }
    if (db_config()->mode == DB_MODE_DOCK) {
        return s.dock_client ? DB_SOURCE_DOCK : DB_SOURCE_UI;
    }
    return s.active_link >= 0 ? (db_source_t)(DB_SOURCE_LINK0 + s.active_link) : DB_SOURCE_UI;
}

static void changed(void)
{
    db_deck_set_writer(wanted_writer());
    xTaskNotifyGive(s.task);
}

/* ---------- screens ---------- */

static void status_screen(ui_screen_t *sc)
{
    db_net_status_t n;
    db_net_status(&n);
    const db_config_t *c = db_config();
    bool dock = c->mode == DB_MODE_DOCK;
    if (!n.lan_up && !n.wlan_up) {
        snprintf(sc->title, sizeof(sc->title), "Kein Netz");
        sc->color = C_BAD;
    } else if (dock) {
        snprintf(sc->title, sizeof(sc->title), "Dock-Modus: warte auf Companion");
        sc->color = C_WAIT;
    } else if (db_link_connected(0) || db_link_connected(1)) {
        snprintf(sc->title, sizeof(sc->title), "Verbunden");
        sc->color = C_OK;
    } else {
        snprintf(sc->title, sizeof(sc->title), c->servers[0][0] ? "Server nicht erreichbar" : "Suche Server");
        sc->color = C_WAIT;
    }
    snprintf(sc->line, sizeof(sc->line), "%s  ·  Menü: Regler 1 und 4 halten",
             n.lan_up ? n.lan_ip : n.wlan_ip);
    char rssi[16];
    snprintf(rssi, sizeof(rssi), "✓ %d dBm", n.rssi);
    ui_set_tile(sc, 0, "LAN", n.lan_up ? "✓ verbunden" : (n.lan_link ? "✗ kein DHCP" : "✗ kein Kabel"),
                n.lan_up ? C_OK : C_BAD);
    ui_set_tile(sc, 1, "WLAN", n.wlan_up ? rssi : (c->wifi_ssid[0] ? "✗ getrennt" : "aus"),
                n.wlan_up ? C_OK : C_TILE);
    for (int i = 0; i < 2; i++) {
        char label[12];
        bool up = db_link_connected(i);
        snprintf(label, sizeof(label), "Server %d", i + 1);
        ui_set_tile(sc, 2 + i, label, up ? (s.active_link == i ? "✓ aktiv" : "✓ bereit") : "✗",
                    up ? C_OK : C_TILE);
    }
    ui_set_tile(sc, 4, "Modus", dock ? "Dock" : "Nativ", C_TILE);
    ui_set_tile(sc, 5, "Firmware", esp_app_get_description()->version, C_TILE);
    ui_set_tile(sc, 6, "C6", s.c6[0] ? s.c6 : "-", C_TILE);
    ui_set_tile(sc, 7, "Menü", "1 + 4 halten", C_TILE);
}

static void update_screen(ui_screen_t *sc)
{
    snprintf(sc->title, sizeof(sc->title), "Update %s: %d %%", s.update_what, s.update_pct);
    snprintf(sc->line, sizeof(sc->line), "Nicht vom Strom trennen");
    sc->color = C_WAIT;
    sc->progress = s.update_pct;
}

/* ---------- rendering ---------- */

static void draw_tile(const ui_tile_t *t, db_rect_t r)
{
    ui_fill(&s.canvas, r, t->color ? t->color : C_TILE);
    r = (db_rect_t){(uint16_t)(r.x + 8), r.y, (uint16_t)(r.w - 16), r.h}; /* the key frame hides the edges */
    ui_text(&s.canvas, &ui_font_medium, r, r.y + r.h / 2 - 4, C_TEXT, t->label);
    ui_text(&s.canvas, &ui_font_small, r, r.y + r.h / 2 + 24, C_DIM, t->sub);
}

static void draw_banner(const ui_screen_t *sc, db_rect_t r)
{
    ui_fill(&s.canvas, r, sc->color);
    const ui_font_t *big =
        ui_text_width(&ui_font_large, sc->title) < r.w - 20 ? &ui_font_large : &ui_font_medium;
    ui_text(&s.canvas, big, r, r.y + r.h / 2 + 4, C_TEXT, sc->title);
    ui_text(&s.canvas, &ui_font_small, r, r.y + r.h - 12, C_TEXT, sc->line);
    if (sc->progress >= 0) {
        db_rect_t bar = {(uint16_t)(r.x + 20), (uint16_t)(r.y + 6),
                         (uint16_t)((r.w - 40) * sc->progress / 100), 8};
        ui_fill(&s.canvas, bar, C_TEXT);
    }
}

static bool ensure_canvas(const db_display_t *d)
{
    if (s.canvas.px && s.canvas.w == d->width && s.canvas.h == d->height) {
        return true;
    }
    heap_caps_free(s.canvas.px);
    s.canvas = (ui_canvas_t){.w = d->width, .h = d->height, .px = jpeg_port_alloc_rgb(d->width, d->height)};
    return s.canvas.px != NULL;
}

/* Copy one rectangle out of the canvas into a fresh encoder buffer and queue it. */
static void submit_rect(db_rect_t r)
{
    const uint8_t *src = s.canvas.px;
    uint8_t *tmp = NULL;
    if (r.w != s.canvas.w || r.h != s.canvas.h) {
        tmp = jpeg_port_alloc_rgb(r.w, r.h);
        if (!tmp) {
            return;
        }
        for (int y = 0; y < r.h; y++) {
            memcpy(tmp + (size_t)y * r.w * 3, s.canvas.px + ((size_t)(r.y + y) * s.canvas.w + r.x) * 3,
                   (size_t)r.w * 3);
        }
        src = tmp;
    }
    uint8_t *jpeg = NULL;
    size_t len = 0;
    esp_err_t err = jpeg_port_encode(src, r.w, r.h, QUALITY, &jpeg, &len);
    heap_caps_free(tmp);
    if (err == ESP_OK) {
        db_deck_submit(DB_SOURCE_UI, 0, r, ++s.seq, jpeg, len, (uint64_t)esp_timer_get_time());
    } else {
        ESP_LOGW(TAG, "encode %ux%u failed: %s", r.w, r.h, esp_err_to_name(err));
    }
}

static void render(const ui_screen_t *sc)
{
    const db_display_t *d = &s.dev.displays[0];
    if (!ensure_canvas(d)) {
        return;
    }
    ui_fill(&s.canvas, (db_rect_t){0, 0, d->width, d->height}, C_BG);
    const db_target_t *full = NULL;
    const db_target_t *strip = NULL;
    for (int i = 0; i < d->target_count; i++) {
        const db_target_t *t = &d->targets[i];
        if (t->kind == DB_TARGET_FULL) {
            full = t;
        } else if (t->kind == DB_TARGET_STRIP) {
            strip = t;
        } else if (t->index < UI_MAX_TILES) {
            draw_tile(&sc->tiles[t->index], t->rect);
        }
    }
    draw_banner(sc, strip ? strip->rect : (db_rect_t){0, 0, d->width, d->height});
    if (full) {
        submit_rect(full->rect);
        return;
    }
    for (int i = 0; i < d->target_count; i++) {
        submit_rect(d->targets[i].rect);
    }
}

static void ui_task(void *arg)
{
    ui_screen_t sc;
    for (;;) {
        ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
        vTaskDelay(pdMS_TO_TICKS(REDRAW_DELAY));
        ulTaskNotifyTake(pdTRUE, 0);
        if (!s.present || db_deck_writer() != DB_SOURCE_UI) {
            continue;
        }
        memset(&sc, 0, sizeof(sc));
        sc.progress = -1;
        if (s.update_pct >= 0) {
            update_screen(&sc);
        } else if (ui_menu_open()) {
            ui_menu_screen(&sc);
        } else {
            status_screen(&sc);
        }
        render(&sc);
    }
}

/* ---------- input ---------- */

static void check_menu_chord(void)
{
    int last_enc = s.dev.encoder_count - 1;
    int last_key = s.dev.key_count - 1;
    bool held = s.dev.encoder_count >= 2 ? (s.enc_down[0] && s.enc_down[last_enc])
                                         : (s.key_down[0] && last_key > 0 && s.key_down[last_key]);
    int64_t now = esp_timer_get_time();
    if (!held) {
        s.chord_since = 0;
    } else if (!s.chord_since) {
        s.chord_since = now;
    } else if (now - s.chord_since >= MENU_HOLD_US && !ui_menu_open()) {
        s.chord_since = 0;
        ESP_LOGI(TAG, "menu opened");
        ui_menu_open_main();
        changed();
    }
}

static void on_input(const db_input_t *ev)
{
    if (ev->kind == DB_INPUT_ENCODER_DOWN || ev->kind == DB_INPUT_ENCODER_UP) {
        s.enc_down[ev->index & 7] = ev->kind == DB_INPUT_ENCODER_DOWN;
    } else if (ev->kind == DB_INPUT_KEY_DOWN || ev->kind == DB_INPUT_KEY_UP) {
        s.key_down[ev->index & 31] = ev->kind == DB_INPUT_KEY_DOWN;
    }
    check_menu_chord();
    if (ui_menu_open() && ui_menu_input(ev)) {
        changed();
    }
}

static void on_device(const db_device_info_t *info)
{
    s.present = info != NULL;
    if (info) {
        s.dev = *info;
        db_deck_set_brightness(db_config()->brightness);
    }
    changed();
}

/* The chord must also fire while the encoders are held still, so poll it. */
static void chord_timer(void *arg)
{
    if (s.chord_since) {
        check_menu_chord();
    }
}

esp_err_t db_ui_start(void)
{
    xTaskCreatePinnedToCore(ui_task, "ui", 6144, NULL, 4, &s.task, 0);
    db_deck_listener_t l = {.input = on_input, .device_changed = on_device};
    db_deck_add_listener(&l);
    db_net_on_change(changed);
    ui_menu_init(changed);
    const esp_timer_create_args_t targs = {.callback = chord_timer, .name = "ui_chord"};
    esp_timer_handle_t timer;
    esp_timer_create(&targs, &timer);
    esp_timer_start_periodic(timer, 200000);
    return ESP_OK;
}

void db_ui_set_active_link(int index)
{
    s.active_link = index;
    changed();
}

void db_ui_set_dock_client(bool connected)
{
    s.dock_client = connected;
    changed();
}

void db_ui_set_update(int pct, const char *what)
{
    s.update_pct = pct;
    snprintf(s.update_what, sizeof(s.update_what), "%s", what ? what : "");
    changed();
}

void db_ui_set_c6_version(const char *version)
{
    snprintf(s.c6, sizeof(s.c6), "%s", version ? version : "");
    changed();
}
