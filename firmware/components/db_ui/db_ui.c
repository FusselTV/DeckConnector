#include "db_ui.h"

#include <stdio.h>
#include <string.h>

#include "canvas.h"
#include "db_config.h"
#include "db_deck.h"
#include "db_link.h"
#include "db_net.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "icons.h"
#include "jpeg_port.h"
#include "menu.h"

#define QUALITY      85
#define REDRAW_DELAY 10 /* ms: the events of one turn or press come in a burst */
#define MENU_HOLD_US 2000000
#define FULL_ABOVE   5 /* more changed areas than this go out as one full image, measured faster */
#define RADIUS       16
#define C_SCREEN     0x000000
#define C_BUTTON     0x1c1c1e
#define C_CARD       0x26262a
#define C_LCD        0x101012
#define C_TRACK      0x3a3a40
#define C_TEXT       0xffffff
#define C_DIM        0x9a9aa0
#define C_UPDATE     0x2f7cf6

static const char *TAG = "ui";

static struct {
    TaskHandle_t task;
    int active_link;
    bool app_client;
    bool companion_client;
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
    bool covered;
    bool full;         /* the next picture goes out whole: someone else drew meanwhile */
    ui_screen_t shown; /* what is on the deck, to send only what changed */
} s = {.active_link = -1, .update_pct = -1, .full = true};

static ui_state_t state(void)
{
    return (ui_state_t){.app_client = s.app_client, .companion_client = s.companion_client, .c6 = s.c6};
}

static db_source_t wanted_writer(void)
{
    if (ui_menu_open() || s.update_pct >= 0) {
        return DB_SOURCE_UI;
    }
    switch (db_config()->controller) {
    case DB_CTRL_APP:
        return s.app_client ? DB_SOURCE_APP : DB_SOURCE_UI;
    case DB_CTRL_COMPANION:
        return s.companion_client ? DB_SOURCE_COMPANION : DB_SOURCE_UI;
    default:
        break;
    }
    return s.active_link >= 0 ? (db_source_t)(DB_SOURCE_LINK0 + s.active_link) : DB_SOURCE_UI;
}

static void pick_writer(void)
{
    /* The menu or an update drew over the active server's picture; tell it to draw again once they end. */
    bool covering = (ui_menu_open() || s.update_pct >= 0) && s.active_link >= 0;
    db_source_t wanted = wanted_writer();
    if (wanted == DB_SOURCE_UI && db_deck_writer() != DB_SOURCE_UI) {
        s.full = true;
    }
    db_deck_set_writer(wanted);
    if (s.covered && !covering && s.active_link >= 0) {
        db_link_resend_role();
    }
    s.covered = covering;
}

static void changed(void)
{
    pick_writer();
    xTaskNotifyGive(s.task);
}

static uint32_t mix(uint32_t a, uint32_t b, float t)
{
    uint32_t out = 0;
    for (int sh = 0; sh <= 16; sh += 8) {
        float va = (float)((a >> sh) & 0xff);
        float vb = (float)((b >> sh) & 0xff);
        out |= (uint32_t)(vb + (va - vb) * t + 0.5f) << sh;
    }
    return out;
}

static const ui_font_t *const value_fonts[] = {&ui_font_medium, &ui_font_label, &ui_font_narrow};
static const ui_font_t *const label_fonts[] = {&ui_font_label, &ui_font_small, &ui_font_narrow_small};
static const ui_font_t *const small_fonts[] = {&ui_font_small, &ui_font_narrow_small};
static const ui_font_t *const title_fonts[] = {&ui_font_large, &ui_font_title, &ui_font_medium,
                                               &ui_font_label};

static db_rect_t inset(db_rect_t r, int dx, int dy)
{
    return (db_rect_t){(uint16_t)(r.x + dx), (uint16_t)(r.y + dy), (uint16_t)(r.w - 2 * dx),
                       (uint16_t)(r.h - 2 * dy)};
}

static void badge(db_rect_t r, ui_badge_t b, uint32_t color)
{
    db_rect_t dot = {(uint16_t)(r.x + r.w - 30), (uint16_t)(r.y + 6), 24, 24};
    ui_round(&s.canvas, dot, 12, b == BADGE_CLOSE ? color : C_TRACK);
    ui_text(&s.canvas, &ui_font_icon_small, dot, dot.y + 21, C_TEXT,
            b == BADGE_CLOSE ? ICON_CLOSE : ICON_OPEN);
}

static void draw_tile(const ui_tile_t *t, db_rect_t r)
{
    ui_fill(&s.canvas, r, C_SCREEN);
    if (t->kind == TILE_NONE) {
        return;
    }
    db_rect_t text = inset(r, 6, 0);
    if (t->kind == TILE_CARD) {
        ui_round(&s.canvas, r, RADIUS, C_CARD);
        int label_x = 8;
        if (t->icon) {
            ui_text_at(&s.canvas, &ui_font_icon_small, (db_rect_t){(uint16_t)(r.x + 7), r.y, 26, r.h},
                       r.y + 29, C_DIM, t->icon, UI_LEFT);
            label_x = 34;
        }
        db_rect_t lab = {(uint16_t)(r.x + label_x), r.y, (uint16_t)(r.w - label_x - 6), r.h};
        ui_text_fit(&s.canvas, small_fonts, 2, lab, r.y + 25, C_DIM, t->label, UI_LEFT);
        ui_text_fit(&s.canvas, value_fonts, 3, text, r.y + 72, C_TEXT, t->value, UI_CENTER);
        ui_text_fit(&s.canvas, small_fonts, 2, text, r.y + 100, C_DIM, t->sub, UI_CENTER);
    } else {
        if (t->active) {
            ui_round(&s.canvas, r, RADIUS, t->color);
            ui_round(&s.canvas, inset(r, 5, 5), RADIUS - 5, mix(t->color, C_BUTTON, 0.22f));
        } else {
            ui_round(&s.canvas, r, RADIUS, C_BUTTON);
        }
        uint32_t icon = t->dim ? C_TRACK : (t->active ? t->color : C_TEXT);
        uint32_t sub = t->active ? mix(t->color, C_TEXT, 0.75f) : C_DIM;
        if (t->icon) {
            ui_text(&s.canvas, &ui_font_icon, r, r.y + 58, icon, t->icon);
        }
        ui_text_fit(&s.canvas, label_fonts, 3, text, r.y + 84, t->dim ? C_DIM : C_TEXT, t->label, UI_CENTER);
        ui_text_fit(&s.canvas, small_fonts, 2, text, r.y + 104, sub, t->sub, UI_CENTER);
    }
    if (t->badge != BADGE_NONE) {
        badge(r, t->badge, t->color);
    }
}

static void draw_segment(const ui_segment_t *g, db_rect_t r)
{
    ui_fill(&s.canvas, r, C_SCREEN);
    db_rect_t box = inset(r, 3, 0);
    ui_round(&s.canvas, box, 10, C_LCD);
    bool wide = g->span > 1;
    int x = 12;
    if (g->icon) {
        const ui_font_t *f = wide ? &ui_font_icon : &ui_font_icon_small;
        ui_text_at(&s.canvas, f, (db_rect_t){(uint16_t)(box.x + 10), box.y, 56, box.h},
                   box.y + (wide ? 70 : 36), g->icon_color, g->icon, UI_LEFT);
        x = wide ? 76 : 40;
    }
    db_rect_t col = {(uint16_t)(box.x + x), box.y, (uint16_t)(box.w - x - 10), box.h};
    ui_text_fit(&s.canvas, small_fonts, 2, col, box.y + 24, C_DIM, g->label, UI_LEFT);
    if (wide) {
        ui_text_fit(&s.canvas, title_fonts, 4, col, box.y + 64, C_TEXT, g->value, UI_LEFT);
        ui_text_fit(&s.canvas, small_fonts, 2, col, box.y + 88, C_DIM, g->sub, UI_LEFT);
        return;
    }
    db_rect_t full = {(uint16_t)(box.x + 12), box.y, (uint16_t)(box.w - 22), box.h};
    ui_text_fit(&s.canvas, title_fonts, 4, g->icon ? col : full, box.y + 66, C_TEXT, g->value, UI_LEFT);
    if (g->bar >= 0) {
        db_rect_t track = {(uint16_t)(box.x + 12), (uint16_t)(box.y + 82), (uint16_t)(box.w - 24), 8};
        ui_round(&s.canvas, track, 4, C_TRACK);
        uint16_t w = (uint16_t)(track.w * (g->bar > 100 ? 100 : g->bar) / 100);
        if (w >= 8) {
            ui_round(&s.canvas, (db_rect_t){track.x, track.y, w, track.h}, 4, g->icon_color);
        }
    } else {
        ui_text_fit(&s.canvas, small_fonts, 2, full, box.y + 88, C_DIM, g->sub, UI_LEFT);
    }
}

static void update_screen(ui_screen_t *sc)
{
    ui_segment_t *g = &sc->seg[0];
    *g = (ui_segment_t){.span = 4, .icon = ICON_UPDATE, .icon_color = C_UPDATE, .bar = -1};
    snprintf(g->label, sizeof(g->label), "Update");
    snprintf(g->value, sizeof(g->value), "Update %s: %d %%", s.update_what, s.update_pct);
    snprintf(g->sub, sizeof(g->sub), "Nicht vom Strom trennen, die Bridge startet danach neu");
    /* decks without a strip show it on the first key */
    ui_tile_t *t = &sc->tiles[0];
    *t = (ui_tile_t){.kind = TILE_CARD, .icon = ICON_UPDATE};
    snprintf(t->label, sizeof(t->label), "Update");
    snprintf(t->value, sizeof(t->value), "%d %%", s.update_pct);
    snprintf(t->sub, sizeof(t->sub), "%s", s.update_what);
}

static bool ensure_canvas(const db_display_t *d)
{
    if (s.canvas.px && s.canvas.w == d->width && s.canvas.h == d->height) {
        return true;
    }
    heap_caps_free(s.canvas.px);
    s.canvas = (ui_canvas_t){.w = d->width, .h = d->height, .px = jpeg_port_alloc_rgb(d->width, d->height)};
    s.full = true;
    return s.canvas.px != NULL;
}

/* Copy one rectangle out of the canvas, encode it and queue it. */
static bool submit_rect(db_rect_t r)
{
    static uint8_t *area; /* reused: one key or a strip part at a time */
    static size_t area_px;
    const uint8_t *src = s.canvas.px;
    if (r.w != s.canvas.w || r.h != s.canvas.h) {
        if ((size_t)r.w * r.h > area_px) {
            heap_caps_free(area);
            area = jpeg_port_alloc_rgb(r.w, r.h);
            area_px = area ? (size_t)r.w * r.h : 0;
            if (!area) {
                return false;
            }
        }
        for (int y = 0; y < r.h; y++) {
            memcpy(area + (size_t)y * r.w * 3, s.canvas.px + ((size_t)(r.y + y) * s.canvas.w + r.x) * 3,
                   (size_t)r.w * 3);
        }
        src = area;
    }
    uint8_t *jpeg = NULL;
    size_t len = 0;
    esp_err_t err = jpeg_port_encode(src, r.w, r.h, QUALITY, &jpeg, &len);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "encode %ux%u failed: %s", r.w, r.h, esp_err_to_name(err));
        return false;
    }
    db_deck_submit(DB_SOURCE_UI, 0, r, ++s.seq, jpeg, len, (uint64_t)esp_timer_get_time());
    return true;
}

static void render(const ui_screen_t *sc)
{
    const db_display_t *d = &s.dev.displays[0];
    if (!ensure_canvas(d)) {
        return;
    }
    const db_target_t *full = NULL;
    const db_target_t *strip = NULL;
    for (int i = 0; i < d->target_count; i++) {
        full = d->targets[i].kind == DB_TARGET_FULL ? &d->targets[i] : full;
        strip = d->targets[i].kind == DB_TARGET_STRIP ? &d->targets[i] : strip;
    }
    bool whole = s.full;
    s.full = false; /* a writer change while drawing sets it again */
    if (whole) {
        ui_fill(&s.canvas, (db_rect_t){0, 0, d->width, d->height}, C_SCREEN);
    }
    db_rect_t dirty[DB_MAX_TARGETS + UI_SEGMENTS];
    int n = 0;
    for (int i = 0; i < d->target_count; i++) {
        const db_target_t *t = &d->targets[i];
        if (t->kind == DB_TARGET_KEY && t->index < UI_MAX_TILES &&
            (whole || memcmp(&sc->tiles[t->index], &s.shown.tiles[t->index], sizeof(ui_tile_t)) != 0)) {
            draw_tile(&sc->tiles[t->index], t->rect);
            dirty[n++] = t->rect;
        } else if (t->kind == DB_TARGET_KEY && t->index >= UI_MAX_TILES && whole) {
            dirty[n++] = t->rect;
        }
    }
    for (int i = 0; strip && i < UI_SEGMENTS;) {
        int span = sc->seg[i].span ? sc->seg[i].span : 1;
        span = i + span > UI_SEGMENTS ? UI_SEGMENTS - i : span;
        bool same = !whole && memcmp(&sc->seg[i], &s.shown.seg[i], sizeof(ui_segment_t) * (size_t)span) == 0;
        uint16_t w = (uint16_t)(strip->rect.w / UI_SEGMENTS);
        db_rect_t r = {(uint16_t)(strip->rect.x + i * w), strip->rect.y, (uint16_t)(w * span), strip->rect.h};
        if (!same) {
            draw_segment(&sc->seg[i], r);
            dirty[n++] = r;
        }
        i += span;
    }
    s.shown = *sc;
    if (n == 0) {
        return;
    }
    bool sent = true;
    if (full && (whole || n > FULL_ABOVE)) {
        sent = submit_rect(full->rect);
    } else {
        for (int i = 0; i < n; i++) {
            sent &= submit_rect(dirty[i]);
        }
    }
    if (!sent) {
        s.full = true; /* what the deck shows is unknown now */
    }
}

static void ui_task(void *arg)
{
    ui_screen_t sc;
    for (;;) {
        ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
        vTaskDelay(pdMS_TO_TICKS(REDRAW_DELAY));
        ulTaskNotifyTake(pdTRUE, 0);
        pick_writer();
        if (!s.present || db_deck_writer() != DB_SOURCE_UI) {
            continue;
        }
        memset(&sc, 0, sizeof(sc));
        if (s.update_pct >= 0) {
            update_screen(&sc);
        } else {
            ui_state_t st = state();
            ui_menu_screen(&sc, &st);
        }
        render(&sc);
    }
}

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
    /* the home screen only takes input while it is what the deck shows, and nothing during an update */
    bool ours = s.update_pct < 0 && (ui_menu_open() || db_deck_writer() == DB_SOURCE_UI);
    if (ours && ui_menu_input(ev)) {
        /* the UI task switches the writer, once this event has reached every listener: a release that closes
         * the menu must not go on to the host that draws next */
        xTaskNotifyGive(s.task);
    }
}

static void on_device(const db_device_info_t *info)
{
    s.present = info != NULL;
    if (info) {
        s.dev = *info;
        s.full = true;
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

void db_ui_set_dock_clients(bool app, bool companion)
{
    s.app_client = app;
    s.companion_client = companion;
    changed();
}

void db_ui_set_update(int pct, const char *what)
{
    s.update_pct = pct;
    snprintf(s.update_what, sizeof(s.update_what), "%s", what ? what : "");
    changed();
}

void db_ui_refresh(void)
{
    changed();
}

void db_ui_set_c6_version(const char *version)
{
    snprintf(s.c6, sizeof(s.c6), "%s", version ? version : "");
    changed();
}
