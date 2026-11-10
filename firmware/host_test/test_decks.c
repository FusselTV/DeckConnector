/*
 * Drives every accepted Stream Deck PID through db_driver_find() against deck_emu and checks the reports
 * against the protocol spec. DECK_PIDS=84,c6 limits the run. Each PID runs in its own process, so a
 * crashing driver fails that PID only.
 */
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>

#include "deck_emu.h"

#define VID 0x0fd9

/* A sanitizer report in a child must show up as a crash, not as a check count. */
const char *__asan_default_options(void);
const char *__asan_default_options(void)
{
    return "abort_on_error=1";
}

static const deck_model_t *M;
static db_dev_t dev;
static unsigned fails;
static uint64_t now_us;

__attribute__((format(printf, 2, 3))) static void fail(int line, const char *fmt, ...)
{
    va_list ap;
    printf("  FAIL 0x%02x line %d: ", M->pid, line);
    va_start(ap, fmt);
    vprintf(fmt, ap);
    va_end(ap);
    putchar('\n');
    fails++;
}

#define CHECK(cond, ...) ((cond) ? (void)0 : fail(__LINE__, __VA_ARGS__))

static void expect_clean(const char *step)
{
    for (size_t i = 0; i < deck_emu_violation_count(); i++) {
        fail(__LINE__, "%s: deck saw a protocol violation: %s", step, deck_emu_violation(i));
    }
    deck_emu_clear();
}

static unsigned image_keys(void)
{
    return M->family == DECK_PEDAL ? 0u : (unsigned)(M->cols * M->rows);
}

static unsigned input_keys(void)
{
    return (unsigned)(M->cols * M->rows + M->sensors);
}

static bool has_full_lcd(void)
{
    return M->family != DECK_SIXKEY && M->family != DECK_PEDAL;
}

/* 90 degrees clockwise, then mirrored. */
#define TRANSPOSE 1090

/* Clockwise degrees the display really gets, -1 for a mirror. Both flips are a 180 degree turn. */
static int orientation(const db_display_t *d)
{
    if (d->rotate == 90 && d->flip_x && !d->flip_y) {
        return TRANSPOSE;
    }
    if (d->flip_x != d->flip_y) {
        return -1;
    }
    return (d->rotate + (d->flip_x ? 180 : 0)) % 360;
}

static const db_target_t *find_strip(uint8_t *display)
{
    for (uint8_t d = 0; d < dev.info.display_count && d < DB_MAX_DISPLAYS; d++) {
        const db_display_t *dp = &dev.info.displays[d];
        for (uint8_t t = 0; t < dp->target_count && t < DB_MAX_TARGETS; t++) {
            if (dp->targets[t].kind == DB_TARGET_STRIP) {
                *display = d;
                return &dp->targets[t];
            }
        }
    }
    return NULL;
}

static void check_info(void)
{
    const db_device_info_t *i = &dev.info;
    CHECK(i->vid == VID && i->pid == M->pid, "info vid:pid %04x:%04x", i->vid, i->pid);
    CHECK(i->key_count == input_keys(), "key_count %u, spec %u", i->key_count, input_keys());
    CHECK(i->encoder_count == M->encoders, "encoder_count %u, spec %u", i->encoder_count, M->encoders);
    CHECK(!strcmp(i->serial, deck_emu_serial()), "serial \"%.32s\", deck reports \"%s\"", i->serial,
          deck_emu_serial());
    CHECK(!strcmp(i->firmware, deck_emu_firmware()), "firmware \"%.16s\", deck reports \"%s\"", i->firmware,
          deck_emu_firmware());
    if (M->family == DECK_PEDAL) {
        CHECK(i->display_count == 0, "display_count %u, the Pedal has no display", i->display_count);
    } else {
        CHECK(i->display_count >= 1 && i->display_count <= DB_MAX_DISPLAYS, "display_count %u",
              i->display_count);
    }

    bool seen[64] = {0};
    unsigned keys = 0, fulls = 0, strips = 0;
    for (uint8_t d = 0; d < i->display_count && d < DB_MAX_DISPLAYS; d++) {
        const db_display_t *dp = &i->displays[d];
        db_format_t fmt = M->family == DECK_SIXKEY ? DB_FORMAT_BMP : DB_FORMAT_JPEG;
        CHECK(dp->format == fmt, "display %u format %d, spec %s", d, dp->format,
              fmt == DB_FORMAT_BMP ? "BMP" : "JPEG");
        CHECK(orientation(dp) == (M->family == DECK_SIXKEY ? TRANSPOSE : M->rotate),
              "display %u rotate %u flip_x %d flip_y %d, spec rotates %u clockwise", d, dp->rotate,
              dp->flip_x, dp->flip_y, M->rotate);
        CHECK(dp->target_count <= DB_MAX_TARGETS, "display %u target_count %u", d, dp->target_count);
        db_rect_t whole = {0, 0, dp->width, dp->height};
        for (uint8_t n = 0; n < dp->target_count && n < DB_MAX_TARGETS; n++) {
            const db_target_t *t = &dp->targets[n];
            db_rect_t r = t->rect;
            CHECK(r.w && r.h && db_rect_contains(whole, r),
                  "display %u target %u rect %u,%u %ux%u outside %ux%u", d, n, r.x, r.y, r.w, r.h, dp->width,
                  dp->height);
            switch (t->kind) {
            case DB_TARGET_KEY:
                keys++;
                CHECK(t->index < image_keys(), "key target index %u, the deck has %u image keys", t->index,
                      image_keys());
                if (t->index < 64) {
                    CHECK(!seen[t->index], "key target %u appears twice", t->index);
                    seen[t->index] = true;
                }
                CHECK(r.w == M->key_px && r.h == M->key_px, "key target %u is %ux%u, key image is %ux%u",
                      t->index, r.w, r.h, M->key_px, M->key_px);
                break;
            case DB_TARGET_FULL:
                fulls++;
                CHECK(has_full_lcd(), "full-display target, but the %s has no full LCD command", M->name);
                CHECK(r.w == M->lcd_w && r.h == M->lcd_h, "full target %ux%u, LCD is %ux%u", r.w, r.h,
                      M->lcd_w, M->lcd_h);
                break;
            case DB_TARGET_STRIP:
                strips++;
                CHECK(M->win_w, "strip target, but the %s has no window", M->name);
                CHECK(r.w == M->win_w && r.h == M->win_h, "strip target %ux%u, window is %ux%u", r.w, r.h,
                      M->win_w, M->win_h);
                break;
            default:
                CHECK(0, "target %u has unknown kind %d", n, t->kind);
            }
        }
    }
    CHECK(keys == image_keys(), "%u key targets, the deck has %u image keys", keys, image_keys());
    /* A full-LCD target is not required, because no source says where the keys sit on the panel. */
    if (M->win_w) {
        CHECK(strips >= 1, "no window target");
    }

    if (!M->touch) {
        CHECK(i->touch_count == 0, "touch_count %u, the %s has no touch window", i->touch_count, M->name);
        return;
    }
    CHECK(i->touch_count == 1, "touch_count %u, expected 1", i->touch_count);
    if (i->touch_count < 1) {
        return;
    }
    uint8_t sd = 0;
    const db_target_t *strip = find_strip(&sd);
    const db_touch_area_t *a = &i->touch[0];
    CHECK(a->rect.w == M->win_w && a->rect.h == M->win_h, "touch area %ux%u, window is %ux%u", a->rect.w,
          a->rect.h, M->win_w, M->win_h);
    CHECK(strip && a->display == sd && db_rect_eq(a->rect, strip->rect),
          "touch area (display %u %u,%u %ux%u) is not the window target", a->display, a->rect.x, a->rect.y,
          a->rect.w, a->rect.h);
}

static size_t make_jpeg(uint16_t w, uint16_t h, size_t want, uint8_t *b)
{
    static const uint8_t sof[] = {0xff, 0xd8, 0xff, 0xc0, 0x00, 0x11, 0x08, 0,    0,    0,   0,
                                  0x03, 0x01, 0x22, 0x00, 0x02, 0x11, 0x01, 0x03, 0x11, 0x01};
    size_t p = sizeof(sof);
    memcpy(b, sof, p);
    b[7] = (uint8_t)(h >> 8);
    b[8] = (uint8_t)h;
    b[9] = (uint8_t)(w >> 8);
    b[10] = (uint8_t)w;
    while (p + 6 <= want) {
        size_t body = want - p - 6;
        if (body > 60000) {
            body = 60000;
        }
        b[p++] = 0xff;
        b[p++] = 0xfe;
        b[p++] = (uint8_t)((body + 2) >> 8);
        b[p++] = (uint8_t)(body + 2);
        for (size_t i = 0; i < body; i++, p++) {
            b[p] = (uint8_t)(1 + p % 0x7d);
        }
    }
    b[p++] = 0xff;
    b[p++] = 0xd9;
    return p;
}

static size_t make_bmp(uint16_t w, uint16_t h, uint8_t *b)
{
    size_t stride = ((size_t)w * 3 + 3) & ~(size_t)3, size = 54 + stride * h;
    memset(b, 0, 54);
    b[0] = 'B';
    b[1] = 'M';
    db_put_u32(b + 2, (uint32_t)size);
    db_put_u32(b + 10, 54);
    db_put_u32(b + 14, 40);
    db_put_u32(b + 18, w);
    db_put_u32(b + 22, h);
    db_put_u16(b + 26, 1);
    db_put_u16(b + 28, 24);
    db_put_u32(b + 34, (uint32_t)(stride * h));
    for (size_t i = 54; i < size; i++) {
        b[i] = (uint8_t)(1 + i % 251);
    }
    return size;
}

static void check_parse_output(const deck_image_t *im, uint8_t display, db_rect_t rect, const uint8_t *img,
                               size_t n, const char *w)
{
    if (!dev.driver->parse_output) {
        return;
    }
    uint8_t *cat = malloc(im->report_count * 1024 + 1);
    size_t got = 0;
    for (size_t k = 0; k < im->report_count; k++) {
        const uint8_t *rep = deck_emu_report(im->first_report + k);
        db_chunk_t c = {0};
        bool ok = dev.driver->parse_output(&dev, rep, 1024, &c);
        CHECK(ok, "%s: parse_output rejects report %zu of its own image", w, k);
        if (!ok) {
            break;
        }
        CHECK(c.part == k, "%s: parse_output part %u, expected %zu", w, c.part, k);
        CHECK(c.last == (k + 1 == im->report_count), "%s: parse_output last %d on report %zu of %zu", w,
              c.last, k, im->report_count);
        CHECK(c.display == display, "%s: parse_output display %u, expected %u", w, c.display, display);
        CHECK(db_rect_eq(c.rect, rect), "%s: parse_output rect %u,%u %ux%u, written %u,%u %ux%u", w, c.rect.x,
              c.rect.y, c.rect.w, c.rect.h, rect.x, rect.y, rect.w, rect.h);
        uintptr_t lo = (uintptr_t)rep, d = (uintptr_t)c.data;
        bool inside = c.data && d >= lo && d + c.len <= lo + 1024;
        CHECK(inside, "%s: parse_output data does not point into the report", w);
        if (!inside) {
            break;
        }
        memcpy(cat + got, c.data, c.len);
        got += c.len;
    }
    if (M->family == DECK_SIXKEY) {
        CHECK(got >= n && !memcmp(cat, img, n), "%s: parse_output pieces do not rebuild the image", w);
    } else {
        CHECK(got == n && !memcmp(cat, img, n),
              "%s: parse_output pieces (%zu bytes) do not rebuild the %zu-byte image", w, got, n);
    }
    free(cat);
}

static void write_target(uint8_t di, const db_target_t *t, db_rect_t rect, size_t want, const char *w)
{
    const db_display_t *dp = &dev.info.displays[di];
    bool swap = dp->rotate % 180 == 90;
    uint16_t uw = swap ? rect.h : rect.w, uh = swap ? rect.w : rect.h;
    uint8_t *img = malloc(dp->format == DB_FORMAT_BMP ? 54 + (size_t)uw * uh * 4 : want + 64);
    size_t n = dp->format == DB_FORMAT_BMP ? make_bmp(uw, uh, img) : make_jpeg(uw, uh, want, img);

    deck_emu_clear();
    db_err_t err = dev.driver->write_image(&dev, t, rect, img, n);
    CHECK(err == DB_OK, "%s: write_image returns %d", w, err);
    deck_emu_check_idle();
    CHECK(deck_emu_image_count() == 1, "%s: deck received %zu complete images, expected 1", w,
          deck_emu_image_count());
    if (deck_emu_image_count() >= 1) {
        const deck_image_t *im = deck_emu_image(0);
        CHECK(im->len == n && !memcmp(im->data, img, n),
              "%s: image arrives with %zu bytes, %zu sent or changed", w, im->len, n);
        bool six = M->family == DECK_SIXKEY;
        switch (t->kind) {
        case DB_TARGET_KEY:
            CHECK(im->cmd == (six ? 0x01 : 0x07) && im->key == t->index, "%s: deck got cmd 0x%02x key %u", w,
                  im->cmd, im->key);
            break;
        case DB_TARGET_FULL:
            CHECK(im->cmd == 0x08, "%s: deck got cmd 0x%02x, expected 0x08", w, im->cmd);
            break;
        case DB_TARGET_STRIP: {
            bool whole = db_rect_eq(rect, t->rect);
            CHECK(im->cmd == 0x0c || (whole && im->cmd == 0x0b), "%s: deck got cmd 0x%02x", w, im->cmd);
            if (im->cmd == 0x0c) {
                db_rect_t want_win = {(uint16_t)(rect.x - t->rect.x), (uint16_t)(rect.y - t->rect.y), rect.w,
                                      rect.h};
                CHECK(db_rect_eq(im->win, want_win),
                      "%s: partial header %u,%u %ux%u, expected window rect %u,%u %ux%u", w, im->win.x,
                      im->win.y, im->win.w, im->win.h, want_win.x, want_win.y, want_win.w, want_win.h);
            }
            break;
        }
        }
        check_parse_output(im, di, rect, img, n, w);
    }
    expect_clean(w);
    free(img);
}

static void check_images(void)
{
    char w[96];
    for (uint8_t d = 0; d < dev.info.display_count && d < DB_MAX_DISPLAYS; d++) {
        const db_display_t *dp = &dev.info.displays[d];
        for (uint8_t n = 0; n < dp->target_count && n < DB_MAX_TARGETS; n++) {
            const db_target_t *t = &dp->targets[n];
            db_rect_t r = t->rect;
            switch (t->kind) {
            case DB_TARGET_KEY:
                snprintf(w, sizeof(w), "display %u key %u image", d, t->index);
                /* gen2: key 0 fills exactly two chunks, key 1 fits in one */
                write_target(d, t, r, t->index == 0 ? 2032 : t->index == 1 ? 700 : 1000 + 150u * t->index, w);
                break;
            case DB_TARGET_FULL:
                snprintf(w, sizeof(w), "display %u full image", d);
                write_target(d, t, r, 9000, w);
                break;
            case DB_TARGET_STRIP:
                snprintf(w, sizeof(w), "display %u window image", d);
                write_target(d, t, r, 4000, w);
                if (t->partial) {
                    db_rect_t sub = {(uint16_t)(r.x + r.w / 4), (uint16_t)(r.y + r.h / 4),
                                     (uint16_t)(r.w / 2), (uint16_t)(r.h / 2)};
                    snprintf(w, sizeof(w), "display %u partial window %u,%u %ux%u", d, sub.x, sub.y, sub.w,
                             sub.h);
                    write_target(d, t, sub, 1500, w);
                }
                break;
            }
        }
    }
    if (dev.driver->parse_output) {
        uint8_t r[1024] = {0};
        db_chunk_t c;
        CHECK(!dev.driver->parse_output(&dev, r, sizeof(r), &c), "parse_output accepts an all-zero report");
        if (M->family == DECK_SIXKEY) {
            r[0] = 0x02;
            r[1] = 0x01;
            CHECK(!dev.driver->parse_output(&dev, r, sizeof(r), &c), "parse_output accepts key byte 0");
            r[5] = 7;
            CHECK(!dev.driver->parse_output(&dev, r, sizeof(r), &c), "parse_output accepts key byte 7");
            r[5] = 1;
            CHECK(!dev.driver->parse_output(&dev, r, sizeof(r) - 1, &c),
                  "parse_output accepts a short report");
        }
    }
}

static void check_brightness(void)
{
    if (!dev.driver->set_brightness) {
        CHECK(M->family == DECK_PEDAL, "no set_brightness");
        return;
    }
    static const uint8_t levels[] = {42, 100, 0};
    for (size_t k = 0; k < sizeof(levels); k++) {
        deck_emu_clear();
        db_err_t err = dev.driver->set_brightness(&dev, levels[k]);
        if (M->family == DECK_PEDAL) {
            CHECK(deck_emu_brightness() == -1, "set_brightness(%u) sent the Pedal a brightness", levels[k]);
        } else {
            CHECK(err == DB_OK, "set_brightness(%u) returns %d", levels[k], err);
            CHECK(deck_emu_brightness() == levels[k], "set_brightness(%u): deck brightness is %d", levels[k],
                  deck_emu_brightness());
        }
        expect_clean("set_brightness");
    }
}

static void expect_event(const uint8_t *rep, size_t len, const db_input_t *want, const char *w)
{
    db_input_t out[8];
    memset(out, 0, sizeof(out));
    now_us += 1000;
    size_t n = dev.driver->parse_input(&dev, rep, len, now_us, out, 8);
    CHECK(n == 1, "%s: parse_input gives %zu events, expected 1", w, n);
    if (n < 1) {
        return;
    }
    const db_input_t *o = &out[0];
    CHECK(o->kind == want->kind, "%s: event kind %d, expected %d", w, o->kind, want->kind);
    CHECK(o->t_us == now_us, "%s: event time %llu, expected %llu", w, (unsigned long long)o->t_us,
          (unsigned long long)now_us);
    switch (want->kind) {
    case DB_INPUT_TOUCH_SWIPE:
        CHECK(o->x2 == want->x2 && o->y2 == want->y2, "%s: swipe end %u,%u, expected %u,%u", w, o->x2, o->y2,
              want->x2, want->y2);
        /* fall through */
    case DB_INPUT_TOUCH_TAP:
    case DB_INPUT_TOUCH_LONG:
        CHECK(o->display == want->display && o->x == want->x && o->y == want->y,
              "%s: touch at display %u %u,%u, expected display %u %u,%u", w, o->display, o->x, o->y,
              want->display, want->x, want->y);
        break;
    case DB_INPUT_ENCODER_TURN:
        CHECK(o->value == want->value, "%s: turn value %d, expected %d", w, o->value, want->value);
        /* fall through */
    default:
        CHECK(o->index == want->index, "%s: index %u, expected %u", w, o->index, want->index);
    }
}

/* build_input must produce the report the deck itself sends, and parse_input must read it back. */
static void round_trip(const db_input_t *ev, const uint8_t *deck, const char *w)
{
    if (!dev.driver->build_input) {
        return;
    }
    uint8_t b[512];
    memset(b, 0xee, sizeof(b));
    size_t n = dev.driver->build_input(&dev, ev, b, 65); /* db_deck's input event holds 65 bytes */
    CHECK(n > 0, "%s: build_input returns 0", w);
    if (!n) {
        return;
    }
    /* touch reports define 10 bytes for a tap or long press and 14 for a flick, whatever the length field
     * says */
    size_t sig = M->family == DECK_SIXKEY ? 7
                 : deck[1] == 0x02        ? (deck[4] == 3 ? 14u : 10u)
                                          : 4u + db_get_u16(deck + 2);
    if (M->family == DECK_SIXKEY) {
        CHECK(n == 65, "%s: build_input report of %zu bytes, the six-key report is 65", w, n);
    }
    CHECK(n >= sig && n <= sizeof(b), "%s: build_input report of %zu bytes, needs %zu", w, n, sig);
    if (n < sig || n > sizeof(b)) {
        return;
    }
    for (size_t i = 0; i < sig; i++) {
        bool fingers = deck[1] == 0x02 && i == 5; /* touch byte 5 is "N/A" in the spec */
        if (!fingers && b[i] != deck[i]) {
            CHECK(0, "%s: build_input byte %zu is 0x%02x, the deck sends 0x%02x", w, i, b[i], deck[i]);
            break;
        }
    }
    for (size_t i = sig; i < n; i++) {
        if (b[i]) {
            CHECK(0, "%s: build_input byte %zu after the payload is 0x%02x, not zero", w, i, b[i]);
            break;
        }
    }
    char w2[96];
    snprintf(w2, sizeof(w2), "%s (round trip)", w);
    expect_event(b, n, ev, w2);
}

static void check_input(void)
{
    if (!dev.driver->parse_input) {
        CHECK(0, "no parse_input");
        return;
    }
    CHECK(dev.driver->build_input != NULL, "no build_input");
    uint8_t rep[512];
    char w[96];
    for (uint8_t k = 0; k < input_keys(); k++) {
        snprintf(w, sizeof(w), "key %u press", k);
        db_input_t down = {.kind = DB_INPUT_KEY_DOWN, .index = k}, up = {.kind = DB_INPUT_KEY_UP, .index = k};
        expect_event(rep, deck_emu_key(k, true, rep, sizeof(rep)), &down, w);
        snprintf(w, sizeof(w), "key %u release", k);
        expect_event(rep, deck_emu_key(k, false, rep, sizeof(rep)), &up, w);
    }
    for (uint8_t k = 0; k < input_keys(); k++) {
        db_input_t down = {.kind = DB_INPUT_KEY_DOWN, .index = k}, up = {.kind = DB_INPUT_KEY_UP, .index = k};
        snprintf(w, sizeof(w), "build key %u press", k);
        deck_emu_key(k, true, rep, sizeof(rep));
        round_trip(&down, rep, w);
        snprintf(w, sizeof(w), "build key %u release", k);
        deck_emu_key(k, false, rep, sizeof(rep));
        round_trip(&up, rep, w);
    }
    static const int8_t turns[] = {3, -2, 1, -128, 127};
    for (uint8_t e = 0; e < M->encoders; e++) {
        db_input_t down = {.kind = DB_INPUT_ENCODER_DOWN, .index = e},
                   up = {.kind = DB_INPUT_ENCODER_UP, .index = e};
        snprintf(w, sizeof(w), "encoder %u press", e);
        expect_event(rep, deck_emu_encoder_press(e, true, rep, sizeof(rep)), &down, w);
        snprintf(w, sizeof(w), "encoder %u release", e);
        expect_event(rep, deck_emu_encoder_press(e, false, rep, sizeof(rep)), &up, w);
        for (size_t k = 0; k < sizeof(turns); k++) {
            db_input_t turn = {.kind = DB_INPUT_ENCODER_TURN, .index = e, .value = turns[k]};
            snprintf(w, sizeof(w), "encoder %u turn %d", e, turns[k]);
            expect_event(rep, deck_emu_encoder_turn(e, turns[k], rep, sizeof(rep)), &turn, w);
            snprintf(w, sizeof(w), "build encoder %u turn %d", e, turns[k]);
            round_trip(&turn, rep, w);
        }
        snprintf(w, sizeof(w), "build encoder %u press", e);
        deck_emu_encoder_press(e, true, rep, sizeof(rep));
        round_trip(&down, rep, w);
        snprintf(w, sizeof(w), "build encoder %u release", e);
        deck_emu_encoder_press(e, false, rep, sizeof(rep));
        round_trip(&up, rep, w);
    }
    if (!M->touch || dev.info.touch_count < 1) {
        return;
    }
    const db_touch_area_t *a = &dev.info.touch[0];
    static const struct {
        uint8_t kind;
        db_input_kind_t ev;
        uint16_t x, y, x2, y2; /* window coordinates; 0xffff = last pixel */
    } touches[] = {
        {1, DB_INPUT_TOUCH_TAP, 10, 20, 0, 0},
        {1, DB_INPUT_TOUCH_TAP, 0xffff, 0xffff, 0, 0},
        {2, DB_INPUT_TOUCH_LONG, 400, 50, 0, 0},
        {3, DB_INPUT_TOUCH_SWIPE, 5, 50, 700, 90},
    };
    for (size_t k = 0; k < sizeof(touches) / sizeof(touches[0]); k++) {
        uint16_t x = touches[k].x == 0xffff ? (uint16_t)(M->win_w - 1) : touches[k].x;
        uint16_t y = touches[k].y == 0xffff ? (uint16_t)(M->win_h - 1) : touches[k].y;
        db_input_t ev = {.kind = touches[k].ev,
                         .display = a->display,
                         .x = (uint16_t)(a->rect.x + x),
                         .y = (uint16_t)(a->rect.y + y),
                         .x2 = (uint16_t)(a->rect.x + touches[k].x2),
                         .y2 = (uint16_t)(a->rect.y + touches[k].y2)};
        size_t len = deck_emu_touch(touches[k].kind, x, y, touches[k].x2, touches[k].y2, rep, sizeof(rep));
        snprintf(w, sizeof(w), "touch kind %u at window %u,%u", touches[k].kind, x, y);
        expect_event(rep, len, &ev, w);
        snprintf(w, sizeof(w), "build touch kind %u at window %u,%u", touches[k].kind, x, y);
        round_trip(&ev, rep, w);
    }
}

static unsigned run_pid(void)
{
    fails = 0;
    deck_emu_start(M->pid);
    dev = (db_dev_t){.io = deck_emu_io(), .driver = db_driver_find(VID, M->pid)};
    CHECK(dev.driver != NULL, "db_driver_find(0x%04x, 0x%04x) finds no driver", VID, M->pid);
    if (!dev.driver) {
        return fails;
    }
    db_err_t err = dev.driver->open(&dev, VID, M->pid);
    CHECK(err == DB_OK, "open returns %d", err);
    expect_clean("open");
    if (err != DB_OK) {
        return fails;
    }
    check_info();
    check_images();
    check_brightness();
    check_input();
    expect_clean("input");
    deck_emu_clear();
    return fails;
}

int main(void)
{
    const deck_model_t *list[64];
    size_t count = 0;
    const char *env = getenv("DECK_PIDS");
    if (env && *env) {
        char buf[256];
        snprintf(buf, sizeof(buf), "%s", env);
        for (char *tok = strtok(buf, ", "); tok && count < 64; tok = strtok(NULL, ", ")) {
            const deck_model_t *m = deck_model((uint16_t)strtoul(tok, NULL, 16));
            if (!m) {
                printf("DECK_PIDS: %s is not an accepted Stream Deck PID\n", tok);
                return 2;
            }
            list[count++] = m;
        }
    } else {
        for (size_t i = 0; i < deck_model_count; i++) {
            list[count++] = &deck_models[i];
        }
    }

    char result[64][32];
    int failed = 0;
    for (size_t i = 0; i < count; i++) {
        M = list[i];
        printf("0x%02x %s\n", M->pid, M->name);
        fflush(stdout);
        pid_t child = fork();
        if (child == 0) {
            unsigned f = run_pid();
            fflush(stdout);
            _exit(f > 250 ? 250 : (int)f);
        }
        int st = 0;
        if (child < 0 || waitpid(child, &st, 0) < 0) {
            perror("fork");
            return 2;
        }
        if (WIFEXITED(st) && WEXITSTATUS(st) == 0) {
            snprintf(result[i], sizeof(result[i]), "PASS");
        } else if (WIFEXITED(st)) {
            snprintf(result[i], sizeof(result[i]), "FAIL (%d%s checks)", WEXITSTATUS(st),
                     WEXITSTATUS(st) == 250 ? "+" : "");
            failed++;
        } else {
            snprintf(result[i], sizeof(result[i]), "CRASH (signal %d)", WTERMSIG(st));
            failed++;
        }
    }

    printf("\n%-6s %-20s %s\n", "PID", "model", "result");
    for (size_t i = 0; i < count; i++) {
        printf("0x%02x   %-20s %s\n", list[i]->pid, list[i]->name, result[i]);
    }
    printf("%zu of %zu PIDs pass\n", count - (size_t)failed, count);
    return failed ? 1 : 0;
}
