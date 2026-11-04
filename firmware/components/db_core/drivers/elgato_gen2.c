/*
 * Elgato Stream Deck, second generation HID protocol (JPEG images in 1024 byte reports).
 * Protocol: https://docs.elgato.com/streamdeck/hid/stream-deck-plus/
 * Adding a model of this family only needs a new row in `models`.
 */
#include <string.h>

#include "drivers.h"

#define ELGATO_VID   0x0fd9
#define REPORT_LEN   1024
#define FEATURE_LEN  32
#define MAX_KEYS     32
#define MAX_ENCODERS 4

enum {
    CMD_KEY = 0x07,
    CMD_FULL = 0x08,
    CMD_STRIP_PARTIAL = 0x0c,
    IN_KEYS = 0x00,
    IN_TOUCH = 0x02,
    IN_ENCODER = 0x03,
    TOUCH_TAP = 1,
    TOUCH_LONG = 2,
    TOUCH_SWIPE = 3,
};

typedef struct {
    uint16_t pid;
    const char *name;
    bool tested;
    uint8_t cols;
    uint8_t rows;
    uint16_t key_px;
    uint16_t width;
    uint16_t height;
    const uint16_t *key_x; /* NULL: keys tile the display without gaps */
    const uint16_t *key_y;
    bool full_image;
    db_rect_t strip; /* w == 0: no strip */
    uint8_t encoders;
    bool flip;
} model_t;

static const uint16_t plus_key_x[] = {13, 232, 451, 670};
static const uint16_t plus_key_y[] = {12, 172};

static const model_t models[] = {
    {.pid = 0x0084,
     .name = "Stream Deck +",
     .tested = true,
     .cols = 4,
     .rows = 2,
     .key_px = 120,
     .width = 800,
     .height = 480,
     .key_x = plus_key_x,
     .key_y = plus_key_y,
     .full_image = true,
     .strip = {0, 380, 800, 100},
     .encoders = 4},
    {.pid = 0x0080,
     .name = "Stream Deck MK.2",
     .cols = 5,
     .rows = 3,
     .key_px = 72,
     .width = 360,
     .height = 216,
     .flip = true},
    {.pid = 0x008f,
     .name = "Stream Deck XL",
     .cols = 8,
     .rows = 4,
     .key_px = 96,
     .width = 768,
     .height = 384,
     .flip = true},
};

typedef struct {
    const model_t *model;
    uint8_t keys[MAX_KEYS];
    uint8_t encoders[MAX_ENCODERS];
} state_t;

_Static_assert(sizeof(state_t) <= sizeof(((db_dev_t *)0)->state), "driver state too large");

static state_t *st(db_dev_t *dev)
{
    return (state_t *)(void *)dev->state;
}

static const model_t *find_model(uint16_t pid)
{
    for (size_t i = 0; i < sizeof(models) / sizeof(models[0]); i++) {
        if (models[i].pid == pid) {
            return &models[i];
        }
    }
    return NULL;
}

static bool match(uint16_t vid, uint16_t pid)
{
    return vid == ELGATO_VID && find_model(pid) != NULL;
}

/* Feature report strings: [id, length, ...]; the text starts at `skip`. */
static void read_feature_string(db_dev_t *dev, uint8_t id, size_t skip, char *out, size_t cap)
{
    uint8_t buf[FEATURE_LEN] = {id};
    size_t n = 0;
    out[0] = '\0';
    if (dev->io->get_feature(buf, sizeof(buf), &n) != DB_OK || n < 2) {
        return;
    }
    size_t end = (size_t)buf[1] + 2;
    if (end > n) {
        end = n;
    }
    size_t len = end > skip ? end - skip : 0;
    if (len >= cap) {
        len = cap - 1;
    }
    memcpy(out, buf + skip, len);
    out[len] = '\0';
}

static void add_target(db_display_t *d, db_target_kind_t kind, uint8_t index, db_rect_t rect, bool partial)
{
    d->targets[d->target_count++] =
        (db_target_t){.kind = kind, .index = index, .rect = rect, .partial = partial};
}

static void fill_display(const model_t *m, db_device_info_t *info)
{
    db_display_t *d = &info->displays[0];
    *d = (db_display_t){.width = m->width,
                        .height = m->height,
                        .format = DB_FORMAT_JPEG,
                        .flip_x = m->flip,
                        .flip_y = m->flip};
    if (m->full_image) {
        add_target(d, DB_TARGET_FULL, 0, (db_rect_t){0, 0, m->width, m->height}, false);
    }
    for (uint8_t r = 0; r < m->rows; r++) {
        for (uint8_t c = 0; c < m->cols; c++) {
            uint16_t x = m->key_x ? m->key_x[c] : (uint16_t)(c * m->key_px);
            uint16_t y = m->key_y ? m->key_y[r] : (uint16_t)(r * m->key_px);
            add_target(d, DB_TARGET_KEY, (uint8_t)(r * m->cols + c), (db_rect_t){x, y, m->key_px, m->key_px},
                       false);
        }
    }
    if (m->strip.w) {
        add_target(d, DB_TARGET_STRIP, 0, m->strip, true);
        info->touch[info->touch_count++] = (db_touch_area_t){.display = 0, .rect = m->strip};
    }
    info->display_count = 1;
}

static db_err_t open_dev(db_dev_t *dev, uint16_t vid, uint16_t pid)
{
    (void)vid;
    const model_t *m = find_model(pid);
    if (!m) {
        return DB_ERR_UNSUPPORTED;
    }
    memset(dev->state, 0, sizeof(dev->state));
    st(dev)->model = m;
    db_device_info_t *info = &dev->info;
    *info = (db_device_info_t){.vendor = "Elgato",
                               .model = m->name,
                               .vid = ELGATO_VID,
                               .pid = pid,
                               .tested = m->tested,
                               .key_count = (uint8_t)(m->cols * m->rows),
                               .encoder_count = m->encoders};
    fill_display(m, info);
    read_feature_string(dev, 0x06, 2, info->serial, sizeof(info->serial));
    read_feature_string(dev, 0x05, 6, info->firmware, sizeof(info->firmware));
    return DB_OK;
}

/* The header layout differs per command; see the Elgato docs. */
static size_t write_header(uint8_t *rep, const db_target_t *t, db_rect_t rect, db_rect_t strip, bool last,
                           uint16_t part, uint16_t n)
{
    rep[0] = 0x02;
    if (t->kind == DB_TARGET_STRIP) {
        rep[1] = CMD_STRIP_PARTIAL;
        db_put_u16(rep + 2, (uint16_t)(rect.x - strip.x));
        db_put_u16(rep + 4, (uint16_t)(rect.y - strip.y));
        db_put_u16(rep + 6, rect.w);
        db_put_u16(rep + 8, rect.h);
        rep[10] = last;
        db_put_u16(rep + 11, part);
        db_put_u16(rep + 13, n);
        return 16;
    }
    rep[1] = t->kind == DB_TARGET_FULL ? CMD_FULL : CMD_KEY;
    rep[2] = t->kind == DB_TARGET_KEY ? t->index : 0;
    rep[3] = last;
    db_put_u16(rep + 4, n);
    db_put_u16(rep + 6, part);
    return 8;
}

static db_err_t write_image(db_dev_t *dev, const db_target_t *t, db_rect_t rect, const uint8_t *data,
                            size_t len)
{
    const model_t *m = st(dev)->model;
    uint8_t rep[REPORT_LEN];
    size_t off = 0;
    uint16_t part = 0;
    while (off < len) {
        size_t hdr = t->kind == DB_TARGET_STRIP ? 16 : 8;
        size_t room = REPORT_LEN - hdr;
        size_t n = len - off < room ? len - off : room;
        memset(rep, 0, sizeof(rep));
        write_header(rep, t, rect, m->strip, off + n >= len, part, (uint16_t)n);
        memcpy(rep + hdr, data + off, n);
        db_err_t err = dev->io->write(rep, sizeof(rep));
        if (err != DB_OK) {
            return err;
        }
        off += n;
        part++;
    }
    return dev->io->flush();
}

static size_t emit(db_input_t *out, size_t max, size_t n, db_input_t ev)
{
    if (n < max) {
        out[n] = ev;
        return n + 1;
    }
    return n;
}

/* Turn the absolute state array into down/up edges. */
static size_t edges(uint8_t *last, const uint8_t *now, size_t count, db_input_kind_t down, uint64_t t,
                    db_input_t *out, size_t max)
{
    size_t n = 0;
    for (size_t i = 0; i < count; i++) {
        uint8_t v = now[i] ? 1 : 0;
        if (v != last[i]) {
            last[i] = v;
            n = emit(out, max, n, (db_input_t){.kind = v ? down : down + 1, .index = (uint8_t)i, .t_us = t});
        }
    }
    return n;
}

static size_t parse_touch(const model_t *m, const uint8_t *r, size_t len, uint64_t t, db_input_t *out,
                          size_t max)
{
    if (len < 14) {
        return 0;
    }
    db_input_t ev = {.kind = DB_INPUT_TOUCH_TAP,
                     .t_us = t,
                     .x = (uint16_t)(db_get_u16(r + 6) + m->strip.x),
                     .y = (uint16_t)(db_get_u16(r + 8) + m->strip.y)};
    switch (r[4]) {
    case TOUCH_TAP:
        ev.kind = DB_INPUT_TOUCH_TAP;
        break;
    case TOUCH_LONG:
        ev.kind = DB_INPUT_TOUCH_LONG;
        break;
    case TOUCH_SWIPE:
        ev.kind = DB_INPUT_TOUCH_SWIPE;
        ev.x2 = (uint16_t)(db_get_u16(r + 10) + m->strip.x);
        ev.y2 = (uint16_t)(db_get_u16(r + 12) + m->strip.y);
        break;
    default:
        return 0;
    }
    return emit(out, max, 0, ev);
}

static size_t parse_encoders(db_dev_t *dev, const uint8_t *r, size_t len, uint64_t t, db_input_t *out,
                             size_t max)
{
    size_t count = st(dev)->model->encoders;
    if (len < 5 + count) {
        return 0;
    }
    if (r[4] == 0x00) {
        return edges(st(dev)->encoders, r + 5, count, DB_INPUT_ENCODER_DOWN, t, out, max);
    }
    size_t n = 0;
    for (size_t i = 0; i < count; i++) {
        int8_t ticks = (int8_t)r[5 + i];
        if (ticks != 0) {
            n = emit(
                out, max, n,
                (db_input_t){.kind = DB_INPUT_ENCODER_TURN, .index = (uint8_t)i, .value = ticks, .t_us = t});
        }
    }
    return n;
}

/* Input report: [0x01 report id, type, length u16, payload...] */
static size_t parse_input(db_dev_t *dev, const uint8_t *r, size_t len, uint64_t t, db_input_t *out,
                          size_t max)
{
    const model_t *m = st(dev)->model;
    if (len < 5 || r[0] != 0x01) {
        return 0;
    }
    switch (r[1]) {
    case IN_KEYS: {
        size_t count = (size_t)m->cols * m->rows;
        return len >= 4 + count ? edges(st(dev)->keys, r + 4, count, DB_INPUT_KEY_DOWN, t, out, max) : 0;
    }
    case IN_TOUCH:
        return parse_touch(m, r, len, t, out, max);
    case IN_ENCODER:
        return parse_encoders(dev, r, len, t, out, max);
    default:
        return 0;
    }
}

static db_err_t set_brightness(db_dev_t *dev, uint8_t pct)
{
    uint8_t buf[FEATURE_LEN] = {0x03, 0x08, pct > 100 ? 100 : pct};
    return dev->io->set_feature(buf, sizeof(buf));
}

const db_driver_t db_driver_elgato_gen2 = {
    .name = "elgato-gen2",
    .match = match,
    .open = open_dev,
    .write_image = write_image,
    .parse_input = parse_input,
    .set_brightness = set_brightness,
};
