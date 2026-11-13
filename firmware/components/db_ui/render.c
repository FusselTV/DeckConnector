#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "icons.h"
#include "ui.h"

#define C_SCREEN 0x000000
#define C_BUTTON 0x1c1c1e
#define C_CARD   0x26262a
#define C_LCD    0x101012
#define C_TRACK  0x3a3a40
#define C_TEXT   0xffffff
#define C_DIM    0x9a9aa0

/* Positions for one key size, in pixels from the key's top left. */
typedef struct {
    int radius;
    int frame;
    const ui_font_t *icon;
    int icon_y;
    int label_y;
    int sub_y;
    const ui_font_t *const *label_fonts;
    int label_n;
    const ui_font_t *const *sub_fonts;
    int sub_n;
    int card_icon_y;
    int card_label_x;
    int card_label_y;
    int value_y;
    const ui_font_t *const *value_fonts;
    int value_n;
    int card_sub_y;
    int badge;
} style_t;

static const ui_font_t *const big_label[] = {&ui_font_label, &ui_font_small, &ui_font_narrow_small};
static const ui_font_t *const big_value[] = {&ui_font_medium, &ui_font_label, &ui_font_narrow};
static const ui_font_t *const small_text[] = {&ui_font_small, &ui_font_narrow_small};
static const ui_font_t *const tiny_text[] = {&ui_font_narrow_small};
static const ui_font_t *const wrap_small[] = {&ui_font_narrow_small, &ui_font_tiny};
static const ui_font_t *const mid_value[] = {&ui_font_label, &ui_font_small, &ui_font_narrow};
static const ui_font_t *const title_fonts[] = {&ui_font_large, &ui_font_title, &ui_font_medium,
                                               &ui_font_label};

static const style_t large = {.radius = 16,
                              .frame = 5,
                              .icon = &ui_font_icon,
                              .icon_y = 58,
                              .label_y = 84,
                              .sub_y = 104,
                              .label_fonts = big_label,
                              .label_n = 3,
                              .sub_fonts = small_text,
                              .sub_n = 2,
                              .card_icon_y = 31,
                              .card_label_x = 36,
                              .card_label_y = 27,
                              .value_y = 72,
                              .value_fonts = big_value,
                              .value_n = 3,
                              .card_sub_y = 100,
                              .badge = 24};
static const style_t medium = {.radius = 13,
                               .frame = 4,
                               .icon = &ui_font_icon_mid,
                               .icon_y = 44,
                               .label_y = 67,
                               .sub_y = 85,
                               .label_fonts = big_label,
                               .label_n = 3,
                               .sub_fonts = small_text,
                               .sub_n = 2,
                               .card_icon_y = 26,
                               .card_label_x = 31,
                               .card_label_y = 22,
                               .value_y = 57,
                               .value_fonts = mid_value,
                               .value_n = 3,
                               .card_sub_y = 80,
                               .badge = 20};
static const style_t small = {.radius = 10,
                              .frame = 3,
                              .icon = &ui_font_icon_mid,
                              .icon_y = 38,
                              .label_y = 55,
                              .sub_y = 68,
                              .label_fonts = small_text,
                              .label_n = 2,
                              .sub_fonts = tiny_text,
                              .sub_n = 1,
                              .card_icon_y = 22,
                              .card_label_x = 27,
                              .card_label_y = 18,
                              .value_y = 44,
                              .value_fonts = small_text,
                              .value_n = 2,
                              .card_sub_y = 63,
                              .badge = 18};

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

static db_rect_t inset(db_rect_t r, int d)
{
    return (db_rect_t){(uint16_t)(r.x + d), (uint16_t)(r.y + d), (uint16_t)(r.w - 2 * d),
                       (uint16_t)(r.h - 2 * d)};
}

static void badge(ui_canvas_t *c, const style_t *st, db_rect_t r, ui_badge_t b, uint32_t color)
{
    int d = st->badge;
    db_rect_t dot = {(uint16_t)(r.x + r.w - d - 5), (uint16_t)(r.y + 5), (uint16_t)d, (uint16_t)d};
    ui_round(c, dot, d / 2, b == BADGE_CLOSE ? color : C_TRACK);
    ui_icon(c, &ui_font_icon_small, dot, C_TEXT, b == BADGE_CLOSE ? ICON_CLOSE : ICON_OPEN);
}

static bool fits(const ui_font_t *const *fonts, int n, int room, const char *s)
{
    for (int i = 0; i < n; i++) {
        if (ui_text_width(fonts[i], s) <= room) {
            return true;
        }
    }
    return false;
}

/* Splits `v` into lines that fit `room` in `f`, breaking after ".", "-" or " " where it can. Returns the
 * count, or 0 when more than `max` lines are needed. */
static int split(const ui_font_t *f, const char *v, int room, int max, size_t *starts)
{
    size_t len = strlen(v);
    int n = 0;
    for (size_t at = 0; at < len; n++) {
        if (n == max) {
            return 0;
        }
        starts[n] = at;
        size_t end = at;
        size_t soft = 0;
        for (size_t k = at + 1; k <= len; k++) {
            if (k < len && (v[k] & 0xc0) == 0x80) {
                continue; /* not inside a UTF-8 character */
            }
            if (ui_text_width_n(f, v + at, v + k) > room) {
                break;
            }
            end = k;
            soft = strchr(".- ", v[k - 1]) ? k : soft;
        }
        if (end == at) {
            return 0;
        }
        at = end < len && soft > at ? soft : end;
    }
    if (n == 2) { /* two lines read best when they are about equal: the separator nearest the middle */
        size_t best = starts[1];
        for (size_t k = 1; k < len; k++) {
            bool better =
                strchr(".- ", v[k - 1]) && abs((int)(2 * k) - (int)len) < abs((int)(2 * best) - (int)len);
            if (better && ui_text_width_n(f, v, v + k) <= room &&
                ui_text_width_n(f, v + k, v + len) <= room) {
                best = k;
            }
        }
        starts[1] = best;
    }
    return n;
}

/* A value too wide for one line goes on two, three on small keys. Returns false when it took one line. */
static bool value_lines(ui_canvas_t *c, const style_t *st, db_rect_t text, int y, const char *v)
{
    if (fits(st->value_fonts, st->value_n, text.w, v)) {
        ui_text_fit(c, st->value_fonts, st->value_n, text, y, C_TEXT, v, UI_CENTER);
        return false;
    }
    const ui_font_t *const *fonts = st == &small ? wrap_small : small_text;
    int max = st == &small ? 3 : 2;
    size_t starts[4];
    for (int i = 0; i < 2; i++) {
        int n = split(fonts[i], v, text.w, max, starts);
        if (n == 0) {
            continue;
        }
        int step = st == &small ? 13 : (st == &large ? 21 : 17);
        int first = y - step * (n - 1) / 2;
        starts[n] = strlen(v);
        for (int k = 0; k < n; k++) {
            char line[48];
            snprintf(line, sizeof(line), "%.*s", (int)(starts[k + 1] - starts[k]), v + starts[k]);
            ui_text_at(c, fonts[i], text, first + k * step, C_TEXT, line, UI_CENTER);
        }
        return true;
    }
    ui_text_fit(c, fonts, 2, text, y, C_TEXT, v,
                UI_CENTER); /* too long for any key: cut, and the test says so */
    return true;
}

/* White with a quiet zone of two modules: a 41 module code (version 6) still gets 2 px modules on a 120 px
 * key. */
static void draw_qr(ui_canvas_t *c, const ui_qr_t *qr, db_rect_t r)
{
    int scale = r.w / (qr->size + 4);
    int side = scale * qr->size;
    int x0 = r.x + (r.w - side) / 2;
    int y0 = r.y + (r.h - side) / 2;
    ui_round_over(c, r, r.w / 12, 0xffffff, C_SCREEN);
    for (int y = 0; y < qr->size; y++) {
        for (int x = 0; x < qr->size; x++) {
            int bit = y * qr->size + x;
            if (qr->bits[bit / 8] & (1 << (bit % 8))) {
                ui_fill(c,
                        (db_rect_t){(uint16_t)(x0 + x * scale), (uint16_t)(y0 + y * scale), (uint16_t)scale,
                                    (uint16_t)scale},
                        0x000000);
            }
        }
    }
}

static void draw_tile(ui_canvas_t *c, const ui_tile_t *t, db_rect_t r, const ui_qr_t *qr)
{
    if (t->kind == TILE_QR && qr->size) {
        draw_qr(c, qr, r);
        return;
    }
    if (t->kind == TILE_NONE) {
        ui_fill(c, r, C_SCREEN);
        return;
    }
    const style_t *st = r.h >= 110 ? &large : (r.h >= 90 ? &medium : &small);
    int pad = r.h >= 90 ? 6 : 4;
    db_rect_t text = {(uint16_t)(r.x + pad), r.y, (uint16_t)(r.w - 2 * pad), r.h};
    if (t->kind == TILE_CARD) {
        if (t->active) { /* a host that draws */
            ui_round_over(c, r, st->radius, t->color, C_SCREEN);
            ui_round(c, inset(r, st->frame), st->radius - st->frame, mix(t->color, C_CARD, 0.22f));
        } else {
            ui_round_over(c, r, st->radius, C_CARD, C_SCREEN);
        }
        int right = t->badge ? st->badge + 6 : pad;
        int label_x = st->card_label_x;
        /* the icon makes room for a name that would not fit next to it */
        if (!t->icon || !fits(st->sub_fonts, st->sub_n, r.w - label_x - right, t->label)) {
            label_x = pad + 2;
        } else {
            /* inside the frame of an active card, not on it */
            ui_text_at(c, &ui_font_icon_small, (db_rect_t){(uint16_t)(r.x + st->frame + 3), r.y, 26, r.h},
                       r.y + st->card_icon_y, C_DIM, t->icon, UI_LEFT);
        }
        db_rect_t lab = {(uint16_t)(r.x + label_x), r.y, (uint16_t)(r.w - label_x - right), r.h};
        ui_text_fit(c, st->sub_fonts, st->sub_n, lab, r.y + st->card_label_y, C_DIM, t->label, UI_LEFT);
        if (!value_lines(c, st, text, r.y + st->value_y, t->value)) {
            ui_text_fit(c, st->sub_fonts, st->sub_n, text, r.y + st->card_sub_y, C_DIM, t->sub, UI_CENTER);
        }
    } else {
        if (t->active) {
            ui_round_over(c, r, st->radius, t->color, C_SCREEN);
            ui_round(c, inset(r, st->frame), st->radius - st->frame, mix(t->color, C_BUTTON, 0.22f));
        } else {
            ui_round_over(c, r, st->radius, C_BUTTON, C_SCREEN);
        }
        uint32_t icon = t->dim ? C_TRACK : (t->active ? t->color : C_TEXT);
        uint32_t sub = t->active ? mix(t->color, C_TEXT, 0.75f) : C_DIM;
        if (t->icon) {
            ui_text(c, st->icon, r, r.y + st->icon_y, icon, t->icon);
        }
        ui_text_fit(c, st->label_fonts, st->label_n, text, r.y + st->label_y, t->dim ? C_DIM : C_TEXT,
                    t->label, UI_CENTER);
        ui_text_fit(c, st->sub_fonts, st->sub_n, text, r.y + st->sub_y, sub, t->sub, UI_CENTER);
    }
    if (t->badge != BADGE_NONE) {
        badge(c, st, r, t->badge, t->color);
    }
}

static void draw_segment(ui_canvas_t *c, const ui_segment_t *g, db_rect_t r)
{
    /* the same 3 px around every side, so no rounded corner runs into the edge of the strip */
    db_rect_t box = inset(r, 3);
    ui_fill(c, r, C_SCREEN);
    ui_round(c, box, 10, C_LCD);
    bool wide = g->span > 1;
    int x = 12;
    if (g->icon) {
        const ui_font_t *f = wide ? &ui_font_icon : &ui_font_icon_small;
        ui_text_at(c, f, (db_rect_t){(uint16_t)(box.x + 10), box.y, 56, box.h}, r.y + (wide ? 70 : 36),
                   g->icon_color, g->icon, UI_LEFT);
        x = wide ? 76 : 40;
    }
    db_rect_t col = {(uint16_t)(box.x + x), box.y, (uint16_t)(box.w - x - 10), box.h};
    db_rect_t full = {(uint16_t)(box.x + 12), box.y, (uint16_t)(box.w - 22), box.h};
    ui_text_fit(c, small_text, 2, col, r.y + 24, C_DIM, g->label, UI_LEFT);
    /* a one-part key with an icon and no bar (Schließen) never in the largest size: "Close" would be twice
     * its German word */
    int top = !wide && g->icon && g->bar < 0;
    ui_text_fit(c, title_fonts + top, 4 - top, wide || g->icon ? col : full, r.y + (wide ? 64 : 66), C_TEXT,
                g->value, UI_LEFT);
    if (g->bar >= 0) {
        db_rect_t track = {(uint16_t)(box.x + 12), (uint16_t)(r.y + 82), (uint16_t)(box.w - 24), 8};
        ui_round(c, track, 4, C_TRACK);
        uint16_t w = (uint16_t)(track.w * (g->bar > 100 ? 100 : g->bar) / 100);
        if (w >= 8) {
            ui_round(c, (db_rect_t){track.x, track.y, w, track.h}, 4, g->icon_color);
        }
    } else {
        ui_text_fit(c, small_text, 2, wide ? col : full, r.y + 88, C_DIM, g->sub, UI_LEFT);
    }
}

ui_layout_t ui_layout_of(const db_device_info_t *info)
{
    ui_layout_t l = {.encoders = info->encoder_count, .strip = false};
    const db_display_t *d = &info->displays[0];
    uint16_t first_y = 0;
    bool seen = false;
    for (uint8_t i = 0; i < d->target_count; i++) {
        const db_target_t *t = &d->targets[i];
        l.strip |=
            t->kind == DB_TARGET_STRIP && info->encoder_count; /* the Neo's info bar is no menu strip */
        if (t->kind == DB_TARGET_KEY && (!seen || t->rect.y == first_y)) {
            first_y = t->rect.y;
            l.key_px = t->rect.h;
            seen = true;
            l.cols = (uint8_t)(l.cols + 1);
        }
    }
    l.rows = (uint8_t)(l.cols ? db_device_image_keys(info) / l.cols : 0);
    return l;
}

int ui_render(ui_canvas_t *c, const db_device_info_t *dev, const ui_screen_t *sc, const ui_screen_t *shown,
              db_rect_t *dirty, int max)
{
    const db_display_t *d = &dev->displays[0];
    const db_target_t *strip = NULL;
    int n = 0;
    for (uint8_t i = 0; i < d->target_count && n < max; i++) {
        const db_target_t *t = &d->targets[i];
        strip = t->kind == DB_TARGET_STRIP && dev->encoder_count ? t : strip;
        if (t->kind == DB_TARGET_KEY && t->index < UI_MAX_KEYS &&
            (!shown || memcmp(&sc->tiles[t->index], &shown->tiles[t->index], sizeof(ui_tile_t)) != 0)) {
            draw_tile(c, &sc->tiles[t->index], t->rect, &sc->qr);
            dirty[n++] = t->rect;
        }
    }
    for (int i = 0; strip && i < UI_SEGMENTS && n < max;) {
        int span = sc->seg[i].span ? sc->seg[i].span : 1;
        span = i + span > UI_SEGMENTS ? UI_SEGMENTS - i : span;
        bool same = shown && memcmp(&sc->seg[i], &shown->seg[i], sizeof(ui_segment_t) * (size_t)span) == 0;
        uint16_t w = (uint16_t)(strip->rect.w / UI_SEGMENTS);
        db_rect_t r = {(uint16_t)(strip->rect.x + i * w), strip->rect.y, (uint16_t)(w * span), strip->rect.h};
        if (!same) {
            draw_segment(c, &sc->seg[i], r);
            dirty[n++] = r;
        }
        i += span;
    }
    return n;
}
