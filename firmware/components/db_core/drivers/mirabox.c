/*
 * Mirabox Stream Dock and the same deck sold by Ajazz, Soomfon and Streonor: "CRT" commands, JPEG keys.
 * Commands, ids and sizes follow Bitfocus' companion-surface-mirabox-stream-dock and Mirabox's
 * StreamDock-Device-SDK, both MIT. host_test/mirabox_ref.h holds what the Bitfocus code sends.
 */
#include <string.h>

#include "drivers.h"

#define MAX_BYTES 10240
#define MAX_KEYS  40
#define MAX_ENC   6
#define IN_LEN    11

typedef struct {
    uint8_t row;
    uint8_t col;
    uint8_t out; /* picture id, 0: none */
    uint8_t in;  /* press id, 0: none */
    uint8_t left;
    uint8_t right; /* turn ids, 0: no encoder */
    uint8_t size;  /* index into family_t w and h */
    bool push;     /* one report per press, none on release */
} ctl_t;

typedef struct {
    const ctl_t *ctl;
    uint8_t count;
    uint16_t w[2];
    uint16_t h[2];
    uint16_t rotate;
    uint16_t packet; /* every output report */
    bool push;
} family_t;

typedef struct {
    uint16_t vid;
    uint16_t pid;
    const char *vendor;
    const char *name;
    const family_t *family;
} model_t;

#define KEY(r, c, o, i) {.row = (r), .col = (c), .out = (o), .in = (i)}
#define ROW5(r, o, i)                                                                                        \
    KEY(r, 0, (o), (i)), KEY(r, 1, (o) + 1, (i) + 1), KEY(r, 2, (o) + 2, (i) + 2),                           \
        KEY(r, 3, (o) + 3, (i) + 3), KEY(r, 4, (o) + 4, (i) + 4)
#define ROW8(r, o, i)                                                                                        \
    KEY(r, 1, (o), (i)), KEY(r, 2, (o) + 1, (i) + 1), KEY(r, 3, (o) + 2, (i) + 2),                           \
        KEY(r, 4, (o) + 3, (i) + 3), KEY(r, 5, (o) + 4, (i) + 4), KEY(r, 6, (o) + 5, (i) + 5),               \
        KEY(r, 7, (o) + 6, (i) + 6), KEY(r, 8, (o) + 7, (i) + 7)
/* 293S ids count down the columns from the right. side: 0 the right column has no input, 2 press only */
#define ROW_293S(r, side)                                                                                    \
    KEY(r, 0, 13 + (r), 13 + (r)), KEY(r, 1, 10 + (r), 10 + (r)), KEY(r, 2, 7 + (r), 7 + (r)),               \
        KEY(r, 3, 4 + (r), 4 + (r)), KEY(r, 4, 1 + (r), 1 + (r)),                                            \
        {.row = (r),                                                                                         \
         .col = 5,                                                                                           \
         .out = 0x10 + (r),                                                                                  \
         .in = (side) ? 0x10 + (r) : 0,                                                                      \
         .size = 1,                                                                                          \
         .push = (side) == 2}
#define KNOB(r, c, i, l, rr, p) {.row = (r), .col = (c), .in = (i), .left = (l), .right = (rr), .push = (p)}

static const ctl_t l_293v3[] = {ROW5(0, 0x0b, 0x01), ROW5(1, 0x06, 0x06), ROW5(2, 0x01, 0x0b)};

static const ctl_t l_xl[] = {
    ROW8(0, 0x19, 0x01), ROW8(1, 0x11, 0x09), ROW8(2, 0x09, 0x11), ROW8(3, 0x01, 0x19), KEY(0, 0, 0, 0x21),
    KEY(1, 0, 0, 0x22),  KEY(2, 0, 0, 0x23),  KEY(0, 9, 0, 0x24),  KEY(1, 9, 0, 0x25),  KEY(2, 9, 0, 0x26),
};

static const ctl_t l_n4[] = {
    ROW5(0, 0x0b, 0x01),
    ROW5(1, 0x06, 0x06),
    {.row = 2, .col = 0, .out = 1, .in = 0x40, .size = 1, .push = true},
    {.row = 2, .col = 1, .out = 2, .in = 0x41, .size = 1, .push = true},
    {.row = 2, .col = 2, .out = 3, .in = 0x42, .size = 1, .push = true},
    {.row = 2, .col = 3, .out = 4, .in = 0x43, .size = 1, .push = true},
    KNOB(3, 0, 0x37, 0xa0, 0xa1, true),
    KNOB(3, 1, 0x35, 0x50, 0x51, true),
    KNOB(3, 2, 0x33, 0x90, 0x91, true),
    KNOB(3, 3, 0x36, 0x70, 0x71, true),
    KNOB(2, 4, 0, 0x38, 0x39, false), /* a swipe on the strip turns it */
};

static const ctl_t l_n3[] = {
    KEY(0, 0, 1, 1),
    KEY(0, 1, 2, 2),
    KEY(0, 2, 3, 3),
    KEY(1, 0, 4, 4),
    KEY(1, 1, 5, 5),
    KEY(1, 2, 6, 6),
    KEY(2, 0, 0, 0x25),
    KEY(2, 1, 0, 0x30),
    KEY(2, 2, 0, 0x31),
    KNOB(0, 3, 0x35, 0x50, 0x51, false),
    KNOB(1, 3, 0x33, 0x90, 0x91, false),
    KNOB(1, 4, 0x34, 0x60, 0x61, false),
};

static const ctl_t l_293s[] = {ROW_293S(0, 1), ROW_293S(1, 1), ROW_293S(2, 1)};
static const ctl_t l_hsv[] = {ROW_293S(0, 2), ROW_293S(1, 2), ROW_293S(2, 2)};
static const ctl_t l_s15[] = {ROW_293S(0, 0), ROW_293S(1, 0), ROW_293S(2, 0)};

#define LAYOUT(l) (l), sizeof(l) / sizeof((l)[0])
static const family_t f_293v3 = {LAYOUT(l_293v3), {112}, {112}, 180, 1024, false};
static const family_t f_xl = {LAYOUT(l_xl), {80}, {80}, 180, 1024, false};
static const family_t f_n4 = {LAYOUT(l_n4), {112, 176}, {112, 124}, 180, 1024, false};
/* Both SDKs: upright and 60 px on vendor 0x6602, else turned by -90. Bitfocus' 270 for the N3 turns nothing
 * in its code: right for 0x6602, which it lists with 64 px; its AKP03E row has -90. */
static const family_t f_n3 = {LAYOUT(l_n3), {64}, {64}, 90, 1024, false};
static const family_t f_n3_6602 = {LAYOUT(l_n3), {60}, {60}, 0, 1024, false};
static const family_t f_n3_6602_64 = {LAYOUT(l_n3), {64}, {64}, 0, 1024, false};
static const family_t f_293s = {LAYOUT(l_hsv), {100, 80}, {100, 80}, 270, 1024, false};
static const family_t f_293s_85 = {LAYOUT(l_293s), {85, 80}, {85, 80}, 270, 512, true};
static const family_t f_293s_96 = {LAYOUT(l_293s), {96, 80}, {96, 80}, 270, 1024, false};
static const family_t f_s15 = {LAYOUT(l_s15), {94, 82}, {94, 82}, 270, 1024, false};

static const model_t models[] = {
    {0x6603, 0x1005, "Mirabox", "Stream Dock 293V3", &f_293v3},
    {0x6603, 0x1006, "Mirabox", "Stream Dock 293V3", &f_293v3},
    {0x6603, 0x1010, "Mirabox", "Stream Dock 293V3", &f_293v3},
    {0x5548, 0x1028, "Mirabox", "Stream Dock XL", &f_xl},
    {0x5548, 0x1031, "Mirabox", "Stream Dock XL", &f_xl},
    {0x6602, 0x1001, "Mirabox", "Stream Dock N4", &f_n4},
    {0x6603, 0x1007, "Mirabox", "Stream Dock N4", &f_n4},
    {0x5548, 0x1008, "Mirabox", "Stream Dock N4 Pro", &f_n4},
    {0x5548, 0x1021, "Mirabox", "Stream Dock N4 Pro", &f_n4},
    {0x5548, 0x1023, "Mirabox", "Stream Dock N4 Pro", &f_n4},
    {0x0300, 0x3004, "Ajazz", "Ajazz AKP05E", &f_n4},
    {0x6602, 0x1000, "Mirabox", "Stream Dock N3", &f_n3_6602},
    {0x6602, 0x1002, "Mirabox", "Stream Dock N3", &f_n3_6602},
    {0x6602, 0x1003, "Mirabox", "Stream Dock N3", &f_n3_6602_64},
    {0x6602, 0x2929, "Mirabox", "Stream Dock N3", &f_n3_6602},
    {0x6603, 0x1002, "Mirabox", "Stream Dock N3", &f_n3},
    {0x6603, 0x1003, "Mirabox", "Stream Dock N3", &f_n3},
    {0x5548, 0x1001, "Mirabox", "Stream Dock N3", &f_n3},
    {0x0300, 0x3002, "Ajazz", "Ajazz AKP03E", &f_n3},
    {0x1500, 0x3001, "Soomfon", "Soomfon CN002", &f_n3},
    {0x6602, 0x1014, "Mirabox", "Stream Dock 293S", &f_293s},
    {0x6603, 0x1014, "Mirabox", "Stream Dock 293S", &f_293s},
    {0x5548, 0x6670, "Mirabox", "Stream Dock 293S", &f_293s_85},
    {0x1500, 0x3003, "Mirabox", "Stream Dock 293S", &f_293s_96},
    {0x5548, 0x6674, "Ajazz", "Ajazz AKP153", &f_293s_85},
    {0x0300, 0x1010, "Ajazz", "Ajazz AKP153", &f_293s_85},
    {0x0300, 0x3010, "Ajazz", "Ajazz AKP153E", &f_293s_96},
    {0x1500, 0x3005, "Streonor", "Streonor S15", &f_s15},
};

typedef struct {
    const model_t *model;
    uint8_t key[MAX_KEYS]; /* the control of each key */
    uint8_t enc[MAX_ENC];
} state_t;

_Static_assert(sizeof(state_t) <= sizeof(((db_dev_t *)0)->state), "driver state too large");

static state_t *st(const db_dev_t *dev)
{
    return (state_t *)dev->state;
}

static const family_t *fam(const db_dev_t *dev)
{
    return st(dev)->model->family;
}

static const model_t *find_model(uint16_t vid, uint16_t pid)
{
    for (size_t i = 0; i < sizeof(models) / sizeof(models[0]); i++) {
        if (models[i].vid == vid && models[i].pid == pid) {
            return &models[i];
        }
    }
    return NULL;
}

static bool match(uint16_t vid, uint16_t pid)
{
    return find_model(vid, pid) != NULL;
}

static db_err_t cmd(db_dev_t *dev, const char *c, size_t n)
{
    uint8_t rep[1024] = {'C', 'R', 'T'};
    memcpy(rep + 5, c, n);
    return dev->io->write(dev->io, rep, fam(dev)->packet);
}

static db_err_t open_dev(db_dev_t *dev, uint16_t vid, uint16_t pid)
{
    const model_t *m = find_model(vid, pid);
    if (!m) {
        return DB_ERR_UNSUPPORTED;
    }
    const family_t *f = m->family;
    memset(dev->state, 0, sizeof(dev->state));
    state_t *s = st(dev);
    s->model = m;
    db_device_info_t *info = &dev->info;
    *info = (db_device_info_t){.vendor = m->vendor,
                               .model = m->name,
                               .vid = vid,
                               .pid = pid,
                               .surface = "streamdock",
                               .display_count = 1};
    db_display_t *d = &info->displays[0];
    *d = (db_display_t){.format = DB_FORMAT_JPEG, .rotate = f->rotate, .max_bytes = MAX_BYTES};
    uint16_t cw = f->w[0] > f->w[1] ? f->w[0] : f->w[1];
    uint16_t ch = f->h[0] > f->h[1] ? f->h[0] : f->h[1];
    for (int pictures = 1; pictures >= 0; pictures--) {
        for (uint8_t i = 0; i < f->count; i++) {
            const ctl_t *c = &f->ctl[i];
            if (c->left || (c->out != 0) != pictures) {
                continue;
            }
            uint8_t k = info->key_count++;
            s->key[k] = i;
            info->tap_keys |= (uint64_t)(!c->in || c->push || f->push) << k;
            info->places[info->place_count++] = (db_place_t){.row = c->row, .col = c->col, .index = k};
            if (c->out) {
                db_rect_t r = {(uint16_t)(c->col * cw), (uint16_t)(c->row * ch), f->w[c->size],
                               f->h[c->size]};
                d->targets[d->target_count++] = (db_target_t){.kind = DB_TARGET_KEY, .index = k, .rect = r};
                d->width = (uint16_t)(r.x + r.w > d->width ? r.x + r.w : d->width);
                d->height = (uint16_t)(r.y + r.h > d->height ? r.y + r.h : d->height);
            }
        }
    }
    for (uint8_t i = 0; i < f->count; i++) {
        const ctl_t *c = &f->ctl[i];
        if (c->left) {
            s->enc[info->encoder_count] = i;
            info->tap_encoders |= (uint8_t)((!c->in || c->push || f->push) << info->encoder_count);
            info->places[info->place_count++] =
                (db_place_t){.row = c->row, .col = c->col, .encoder = true, .index = info->encoder_count++};
        }
    }
    db_err_t err = cmd(dev, "DIS", 3);
    if (err == DB_OK) {
        err = cmd(dev, "CLE\0\0\0\xff", 7);
    }
    if (err == DB_OK) {
        err = cmd(dev, "LBLIG\0", 6);
    }
    return err;
}

/* [BAT, size u32 big endian, id] and the JPEG in plain reports of `packet` bytes, then STP shows it. */
static db_err_t write_image(db_dev_t *dev, const db_target_t *t, db_rect_t rect, const uint8_t *data,
                            size_t len)
{
    (void)rect;
    const family_t *f = fam(dev);
    if (t->kind != DB_TARGET_KEY || t->index >= dev->info.key_count || len > MAX_BYTES) {
        return DB_ERR_PROTO;
    }
    char bat[8] = "BAT";
    for (int i = 0; i < 4; i++) {
        bat[3 + i] = (char)(len >> (24 - 8 * i));
    }
    bat[7] = (char)f->ctl[st(dev)->key[t->index]].out;
    db_err_t err = cmd(dev, bat, sizeof(bat));
    uint8_t rep[1024];
    for (size_t off = 0; err == DB_OK && off < len; off += f->packet) {
        size_t n = len - off < f->packet ? len - off : f->packet;
        memset(rep, 0, f->packet);
        memcpy(rep, data + off, n);
        err = dev->io->write(dev->io, rep, f->packet);
    }
    if (err == DB_OK) {
        err = cmd(dev, "STP", 3);
    }
    return err == DB_OK ? dev->io->flush(dev->io) : err;
}

static size_t emit(db_input_t *out, size_t max, size_t n, db_input_kind_t kind, uint8_t index, int16_t value,
                   uint64_t t)
{
    if (n < max) {
        out[n++] = (db_input_t){.kind = kind, .index = index, .value = value, .t_us = t};
    }
    return n;
}

/* Press and release, or both at once from a control that reports only the press. */
static size_t press(db_input_t *out, size_t max, bool push, bool down, db_input_kind_t kind, uint8_t index,
                    uint64_t t)
{
    size_t n = emit(out, max, 0, push || down ? kind : kind + 1, index, 0, t);
    return push ? emit(out, max, n, kind + 1, index, 0, t) : n;
}

/* Input report: ["ACK", 0, 0, "OK", 0, 0, id, state] */
static size_t parse_input(db_dev_t *dev, const uint8_t *r, size_t len, uint64_t t, db_input_t *out,
                          size_t max)
{
    if (len < IN_LEN || memcmp(r, "ACK", 3) != 0 || !r[9]) {
        return 0;
    }
    const family_t *f = fam(dev);
    for (uint8_t k = 0; k < dev->info.key_count; k++) {
        const ctl_t *c = &f->ctl[st(dev)->key[k]];
        if (c->in == r[9]) {
            return press(out, max, c->push || f->push, r[10] != 0, DB_INPUT_KEY_DOWN, k, t);
        }
    }
    for (uint8_t e = 0; e < dev->info.encoder_count; e++) {
        const ctl_t *c = &f->ctl[st(dev)->enc[e]];
        if (c->in == r[9]) {
            return press(out, max, c->push || f->push, r[10] != 0, DB_INPUT_ENCODER_DOWN, e, t);
        }
        if (c->left == r[9] || c->right == r[9]) {
            return emit(out, max, 0, DB_INPUT_ENCODER_TURN, e, c->right == r[9] ? 1 : -1, t);
        }
    }
    return 0;
}

static size_t build_input(const db_dev_t *dev, const db_input_t *ev, uint8_t *r, size_t cap)
{
    const family_t *f = fam(dev);
    const ctl_t *c = NULL;
    bool key = ev->kind == DB_INPUT_KEY_DOWN || ev->kind == DB_INPUT_KEY_UP;
    bool down = ev->kind == DB_INPUT_KEY_DOWN || ev->kind == DB_INPUT_ENCODER_DOWN;
    if (key && ev->index < dev->info.key_count) {
        c = &f->ctl[st(dev)->key[ev->index]];
    } else if (!key && ev->index < dev->info.encoder_count) {
        c = &f->ctl[st(dev)->enc[ev->index]];
    }
    uint8_t id = 0;
    if (c && ev->kind == DB_INPUT_ENCODER_TURN) {
        id = ev->value > 0 ? c->right : ev->value < 0 ? c->left : 0;
    } else if (c && ev->kind <= DB_INPUT_ENCODER_UP && (down || !(c->push || f->push))) {
        id = c->in;
    }
    if (!id || cap < IN_LEN) {
        return 0;
    }
    static const uint8_t head[] = {'A', 'C', 'K', 0, 0, 'O', 'K', 0, 0};
    memcpy(r, head, sizeof(head));
    r[9] = id;
    r[10] = down;
    return IN_LEN;
}

/* round(100 * (pct / 100) ^ 0.75) as the Bitfocus code sends it: the fourth root of pct^3 * 100 */
static db_err_t set_brightness(db_dev_t *dev, uint8_t pct)
{
    uint32_t v = (uint32_t)(pct > 100 ? 100 : pct);
    v = 16 * v * v * v * 100;
    uint32_t b = 0;
    while ((2 * b + 1) * (2 * b + 1) * (2 * b + 1) * (2 * b + 1) <= v) {
        b++;
    }
    char c[] = {'L', 'I', 'G', 0, 0, (char)b};
    return cmd(dev, c, sizeof(c));
}

static db_err_t tick(db_dev_t *dev)
{
    return cmd(dev, "CONNECT", 7);
}

const db_driver_t db_driver_mirabox = {
    .name = "mirabox",
    .match = match,
    .open = open_dev,
    .write_image = write_image,
    .parse_input = parse_input,
    .set_brightness = set_brightness,
    .build_input = build_input,
    .tick = tick,
    .tick_ms = 8000,
};
