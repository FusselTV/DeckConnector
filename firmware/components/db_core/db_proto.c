#include "db_proto.h"

#include <stdio.h>
#include <string.h>

void db_proto_hdr(uint8_t *out, uint8_t type, uint8_t deck)
{
    out[0] = type;
    out[1] = deck;
    out[2] = out[3] = 0;
}

void db_proto_shown(uint8_t out[DB_SHOWN_LEN], uint8_t deck, bool stream, uint32_t id, uint32_t us)
{
    db_proto_hdr(out, DB_MSG_SHOWN, deck);
    out[4] = stream;
    db_put_u32(out + 5, id);
    db_put_u32(out + 9, us);
}

void db_proto_credit(uint8_t out[DB_CREDIT_LEN], uint8_t deck, uint8_t display, uint32_t bytes,
                     uint8_t frames)
{
    db_proto_hdr(out, DB_MSG_CREDIT, deck);
    out[4] = display;
    db_put_u32(out + 5, bytes);
    out[9] = frames;
}

void db_proto_input(uint8_t out[DB_INPUT_LEN], uint8_t deck, uint32_t seq, uint64_t usb_time_us,
                    const db_input_t *ev)
{
    static const uint8_t kinds[] = {
        [DB_INPUT_KEY_DOWN] = 0,     [DB_INPUT_KEY_UP] = 1,      [DB_INPUT_ENCODER_TURN] = 2,
        [DB_INPUT_ENCODER_DOWN] = 3, [DB_INPUT_ENCODER_UP] = 4,  [DB_INPUT_TOUCH_TAP] = 5,
        [DB_INPUT_TOUCH_LONG] = 6,   [DB_INPUT_TOUCH_SWIPE] = 7,
    };
    db_proto_hdr(out, DB_MSG_INPUT, deck);
    db_put_u32(out + 4, seq);
    db_put_u64(out + 8, usb_time_us);
    out[16] = kinds[ev->kind];
    out[17] = ev->index;
    db_put_u16(out + 18, (uint16_t)ev->value);
    db_put_u16(out + 20, ev->x);
    db_put_u16(out + 22, ev->y);
    db_put_u16(out + 24, ev->x2);
    db_put_u16(out + 26, ev->y2);
}

/* A key by its number; another kind by its place among those, *n counting down. */
static const db_target_t *find(const db_device_info_t *info, db_target_kind_t kind, int *n, uint8_t *display)
{
    for (uint8_t d = 0; d < info->display_count; d++) {
        for (uint8_t i = 0; i < info->displays[d].target_count; i++) {
            const db_target_t *t = &info->displays[d].targets[i];
            if (t->kind == kind && (kind == DB_TARGET_KEY ? t->index == *n : *n == 0)) {
                *display = d;
                return t;
            }
            *n -= t->kind == kind && kind != DB_TARGET_KEY;
        }
    }
    return NULL;
}

const db_target_t *db_proto_target(const db_device_info_t *info, uint8_t id, uint8_t *display)
{
    int keys = db_device_image_keys(info);
    int n = id < keys ? id : id - keys;
    if (id < keys) {
        return find(info, DB_TARGET_KEY, &n, display);
    }
    const db_target_t *t = find(info, DB_TARGET_STRIP, &n, display);
    return t ? t : find(info, DB_TARGET_FULL, &n, display);
}

bool db_proto_format_ok(const db_display_t *d, uint8_t format)
{
    return format == (d->format == DB_FORMAT_JPEG ? DB_IMG_JPEG : DB_IMG_BMP);
}

/* NOLINTNEXTLINE(readability-non-const-parameter): the writer fills it */
void db_cbor_begin(db_cbor_t *c, uint8_t *buf, size_t cap)
{
    *c = (db_cbor_t){.buf = buf, .cap = cap};
}

static void put(db_cbor_t *c, const void *data, size_t n)
{
    if (c->over || c->cap - c->len < n) {
        c->over = true;
        return;
    }
    memcpy(c->buf + c->len, data, n);
    c->len += n;
}

static void head(db_cbor_t *c, uint8_t major, uint64_t v)
{
    uint8_t b[9];
    size_t n = 1;
    uint8_t ai = v < 24 ? (uint8_t)v : v <= 0xff ? 24 : v <= 0xffff ? 25 : v <= 0xffffffffU ? 26 : 27;
    int bytes = ai < 24 ? 0 : 1 << (ai - 24);
    b[0] = (uint8_t)(major << 5 | ai);
    for (int i = bytes - 1; i >= 0; i--) {
        b[n++] = (uint8_t)(v >> (8 * i));
    }
    put(c, b, n);
}

static void text(db_cbor_t *c, const char *s)
{
    size_t n = strlen(s);
    head(c, 3, n);
    put(c, s, n);
}

static void key(db_cbor_t *c, const char *k)
{
    if (k) {
        text(c, k);
    }
}

void db_cbor_map(db_cbor_t *c, const char *k)
{
    key(c, k);
    put(c, "\xbf", 1);
}

void db_cbor_arr(db_cbor_t *c, const char *k)
{
    key(c, k);
    put(c, "\x9f", 1);
}

void db_cbor_close(db_cbor_t *c)
{
    put(c, "\xff", 1);
}

void db_cbor_str(db_cbor_t *c, const char *k, const char *v)
{
    key(c, k);
    text(c, v);
}

void db_cbor_uint(db_cbor_t *c, const char *k, uint64_t v)
{
    key(c, k);
    head(c, 0, v);
}

void db_cbor_bool(db_cbor_t *c, const char *k, bool v)
{
    key(c, k);
    put(c, v ? "\xf5" : "\xf4", 1);
}

size_t db_cbor_end(db_cbor_t *c)
{
    return c->over ? 0 : c->len;
}

/* Major type and argument of the item at *p; `indef` for a length of 31. False past the end. */
static bool read_head(const uint8_t *b, size_t n, size_t *p, uint8_t *major, uint64_t *arg, bool *indef)
{
    if (*p >= n) {
        return false;
    }
    uint8_t ib = b[(*p)++];
    uint8_t ai = ib & 31;
    *major = ib >> 5;
    *indef = ai == 31;
    *arg = ai;
    if (ai < 24 || *indef) {
        return !*indef || (*major >= 2 && *major <= 5);
    }
    if (ai > 27) {
        return false;
    }
    size_t k = (size_t)1 << (ai - 24);
    if (n - *p < k) {
        return false;
    }
    *arg = 0;
    for (size_t i = 0; i < k; i++) {
        *arg = *arg << 8 | b[(*p)++];
    }
    return true;
}

/* NOLINTNEXTLINE(misc-no-recursion): `depth` stops it at 8 */
static bool skip(const uint8_t *b, size_t n, size_t *p, int depth)
{
    uint8_t m;
    uint64_t a;
    bool indef;
    if (depth > 8 || !read_head(b, n, p, &m, &a, &indef)) {
        return false;
    }
    if (m == 6) {
        return skip(b, n, p, depth + 1);
    }
    if (m < 2 || m == 7) {
        return true;
    }
    if (indef) {
        while (*p < n && b[*p] != 0xff) {
            if (!skip(b, n, p, depth + 1)) {
                return false;
            }
        }
        if (*p >= n) {
            return false;
        }
        (*p)++; /* the break */
        return true;
    }
    if (m < 4) {
        if (a > n - *p) {
            return false;
        }
        *p += (size_t)a;
        return true;
    }
    if (a > n) { /* every item takes a byte at least */
        return false;
    }
    for (uint64_t i = 0; i < (m == 5 ? 2 * a : a); i++) {
        if (!skip(b, n, p, depth + 1)) {
            return false;
        }
    }
    return true;
}

bool db_cbor_text(const uint8_t *buf, size_t len, const char *k, char *out, size_t cap)
{
    size_t p = 0;
    uint8_t m;
    uint64_t pairs;
    bool indef;
    if (!read_head(buf, len, &p, &m, &pairs, &indef) || m != 5) {
        return false;
    }
    for (uint64_t i = 0; indef ? p < len && buf[p] != 0xff : i < pairs; i++) {
        size_t at = p;
        uint64_t n;
        bool kin;
        bool match = read_head(buf, len, &p, &m, &n, &kin) && m == 3 && !kin && n <= len - p &&
                     n == strlen(k) && memcmp(buf + p, k, (size_t)n) == 0;
        p = at;
        if (!skip(buf, len, &p, 1)) {
            return false;
        }
        if (match) {
            if (!read_head(buf, len, &p, &m, &n, &kin) || m != 3 || kin || n > len - p || n >= cap) {
                return false;
            }
            memcpy(out, buf + p, (size_t)n);
            out[n] = '\0';
            return true;
        }
        if (!skip(buf, len, &p, 1)) {
            return false;
        }
    }
    return false;
}

/* "Stream Deck + XL" -> "streamdeck-plus-xl", "Stream Dock 293V3" -> "streamdock-293v3" */
static void slug(const char *name, char *out, size_t cap)
{
    size_t n = 0;
    for (const char *c = name; *c && n + 5 < cap; c++) {
        if (*c == ' ' && c - name == 6 && strncmp(name, "Stream ", 7) == 0) {
            continue;
        }
        if (*c == '+') {
            n += (size_t)snprintf(out + n, cap - n, "plus");
        } else if (*c == ' ') {
            out[n++] = '-';
        } else if (*c != '.') {
            out[n++] = (char)(*c >= 'A' && *c <= 'Z' ? *c + 32 : *c);
        }
    }
    out[n] = '\0';
}

static void rect(db_cbor_t *c, db_rect_t r)
{
    db_cbor_uint(c, "x", r.x);
    db_cbor_uint(c, "y", r.y);
    db_cbor_uint(c, "w", r.w);
    db_cbor_uint(c, "h", r.h);
}

void db_proto_deck(db_cbor_t *c, const char *k, int deck, const db_device_info_t *info)
{
    static const char *const kinds[] = {
        [DB_TARGET_FULL] = "full", [DB_TARGET_KEY] = "key", [DB_TARGET_STRIP] = "strip"};
    char model[40];
    slug(info->model, model, sizeof(model));
    db_cbor_map(c, k);
    db_cbor_uint(c, "deck", (uint64_t)deck);
    db_cbor_str(c, "serial", info->serial);
    db_cbor_str(c, "model", model);
    db_cbor_str(c, "name", info->model);
    db_cbor_arr(c, "displays");
    for (uint8_t i = 0; i < info->display_count; i++) {
        const db_display_t *d = &info->displays[i];
        db_cbor_map(c, NULL);
        db_cbor_uint(c, "id", i);
        db_cbor_uint(c, "w", d->width);
        db_cbor_uint(c, "h", d->height);
        db_cbor_arr(c, "formats");
        db_cbor_str(c, NULL, d->format == DB_FORMAT_JPEG ? "jpeg" : "bmp");
        db_cbor_close(c);
        /* flip_y is a half turn and flip_x: one mirror after the turn says it all */
        db_cbor_uint(c, "rotate", (d->rotate + (d->flip_y ? 180U : 0U)) % 360);
        db_cbor_bool(c, "flip", d->flip_x != d->flip_y);
        db_cbor_uint(c, "max_bytes", d->max_bytes ? d->max_bytes : DB_PROTO_IMAGE_MAX);
        db_cbor_close(c);
    }
    db_cbor_close(c);
    db_cbor_arr(c, "targets");
    uint8_t display = 0;
    for (unsigned id = 0; id < 256; id++) {
        const db_target_t *t = db_proto_target(info, (uint8_t)id, &display);
        if (!t) {
            break;
        }
        db_cbor_map(c, NULL);
        db_cbor_uint(c, "id", id);
        db_cbor_str(c, "kind", kinds[t->kind]);
        db_cbor_uint(c, "display", display);
        rect(c, t->rect);
        db_cbor_close(c);
    }
    db_cbor_close(c);
    db_cbor_uint(c, "keys", info->key_count);
    db_cbor_uint(c, "encoders", info->encoder_count);
    db_cbor_arr(c, "touch");
    for (uint8_t i = 0; i < info->touch_count; i++) {
        db_cbor_map(c, NULL);
        rect(c, info->touch[i].rect);
        db_cbor_close(c);
    }
    db_cbor_close(c);
    db_cbor_close(c);
}
