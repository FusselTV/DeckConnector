#include "deck_emu.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define OUT_LEN          1024
#define FEATURE_LEN      32
#define INPUT_LEN        512
#define SIXKEY_INPUT_LEN 65
#define MAX_REPORTS      512
#define MAX_IMAGES       16
#define MAX_VIOLATIONS   64

#define CLASSIC(pid, name) {pid, name, DECK_CLASSIC, 5, 3, 0, 72, 480, 272, 0, 0, false, 0, 180, 0}
#define XL(pid, name)      {pid, name, DECK_XL, 8, 4, 0, 96, 1024, 600, 0, 0, false, 0, 180, 0}
#define SIXKEY(pid, name)  {pid, name, DECK_SIXKEY, 3, 2, 0, 80, 320, 240, 0, 0, false, 0, 90, 0}

const deck_model_t deck_models[] = {
    SIXKEY(0x63, "Mini"),
    XL(0x6c, "XL"),
    CLASSIC(0x6d, "Stream Deck (v2)"),
    CLASSIC(0x80, "MK.2"),
    {0x84, "+", DECK_PLUS, 4, 2, 0, 120, 800, 480, 800, 100, true, 4, 0, 0x0a},
    {0x86, "Pedal", DECK_PEDAL, 3, 1, 0, 0, 0, 0, 0, 0, false, 0, 0, 0},
    XL(0x8f, "XL (2022)"),
    SIXKEY(0x90, "Mini MK.2"),
    {0x9a, "Neo", DECK_NEO, 4, 2, 2, 96, 480, 320, 248, 58, false, 0, 180, 0},
    CLASSIC(0xa5, "MK.2 Scissor"),
    SIXKEY(0xb3, "Mini Discord"),
    SIXKEY(0xb8, "6 Module"),
    CLASSIC(0xb9, "15 Module"),
    XL(0xba, "32 Module"),
    /* the + XL page gives a tap report length of 0x10, unlike the + */
    {0xc6, "+ XL", DECK_PLUS, 9, 4, 0, 112, 1280, 800, 1200, 100, true, 6, 270, 0x10},
    CLASSIC(0xe4, "15 Module Scissor"),
};
const size_t deck_model_count = sizeof(deck_models) / sizeof(deck_models[0]);

static const char gen2_serial[] = "AL31K1A01234";
static const char gen2_fw[] = "1.00.000";
static const char sixkey_serial[] = "BL31K1A05678";
static const char sixkey_fw[] = "3.00.000";

typedef struct {
    db_hid_io_t io; /* first: the io callbacks find their deck by it */
    const deck_model_t *m;
    char serial[sizeof(gen2_serial)];
    char violations[MAX_VIOLATIONS][200];
    size_t nviol;
    uint8_t reports[MAX_REPORTS][OUT_LEN];
    size_t nreports;
    deck_image_t images[MAX_IMAGES];
    size_t nimages;
    int brightness;
    uint8_t keys[64];
    uint8_t enc_down[8];
    struct {
        bool active;
        uint8_t cmd, key;
        db_rect_t win;
        uint16_t next;
        uint8_t *data;
        size_t len;
        size_t first_report;
    } x;
} emu_t;

static emu_t decks[DECK_EMU_DECKS];
static emu_t *emu = decks; /* the deck the deck_emu_* calls are about */
void (*deck_emu_sink)(int deck, const deck_image_t *im);

__attribute__((format(printf, 1, 2))) static void violation(const char *fmt, ...)
{
    if (emu->nviol < MAX_VIOLATIONS || deck_emu_sink) {
        char *v = emu->violations[emu->nviol % MAX_VIOLATIONS];
        va_list ap;
        va_start(ap, fmt);
        vsnprintf(v, sizeof(emu->violations[0]), fmt, ap);
        va_end(ap);
        if (deck_emu_sink) {
            fprintf(stderr, "deck_emu %s: %s\n", emu->serial, v);
        }
    }
    emu->nviol++;
}

const deck_model_t *deck_model(uint16_t pid)
{
    for (size_t i = 0; i < deck_model_count; i++) {
        if (deck_models[i].pid == pid) {
            return &deck_models[i];
        }
    }
    return NULL;
}

static bool sixkey(void)
{
    return emu->m->family == DECK_SIXKEY;
}

static unsigned input_keys(void)
{
    return emu->m->cols * emu->m->rows + emu->m->sensors;
}

static uint16_t be16(const uint8_t *p)
{
    return (uint16_t)(p[0] << 8 | p[1]);
}

static void rotated(uint16_t w, uint16_t h, uint16_t *ow, uint16_t *oh)
{
    bool swap = emu->m->rotate % 180 == 90;
    *ow = swap ? h : w;
    *oh = swap ? w : h;
}

static void what(char *buf, size_t cap, uint8_t cmd, uint8_t key, db_rect_t win)
{
    switch (cmd) {
    case 0x01:
    case 0x07:
        snprintf(buf, cap, "key %u image (cmd 0x%02x)", key, cmd);
        break;
    case 0x08:
        snprintf(buf, cap, "full LCD image (cmd 0x08)");
        break;
    case 0x0b:
        snprintf(buf, cap, "window image (cmd 0x0b)");
        break;
    default:
        snprintf(buf, cap, "partial window %u,%u %ux%u (cmd 0x0c)", win.x, win.y, win.w, win.h);
        break;
    }
}

static void check_jpeg(const char *w, deck_image_t *im, uint16_t ew, uint16_t eh)
{
    const uint8_t *d = im->data;
    size_t n = im->len;
    if (n < 4 || d[0] != 0xff || d[1] != 0xd8) {
        violation("%s: does not start with JPEG SOI FFD8", w);
        return;
    }
    if (d[n - 2] != 0xff || d[n - 1] != 0xd9) {
        violation("%s: does not end with JPEG EOI FFD9", w);
    }
    for (size_t p = 2; p + 4 <= n;) {
        uint8_t m = d[p + 1];
        if (d[p] != 0xff) {
            violation("%s: JPEG marker expected at byte %zu", w, p);
            return;
        }
        if (m == 0xff) {
            p++;
            continue;
        }
        if (m == 0xd9 || m == 0xda) {
            break;
        }
        if (m == 0xc0) {
            if (p + 9 > n) {
                break;
            }
            uint16_t ih = be16(d + p + 5), iw = be16(d + p + 7);
            if (iw != ew || ih != eh) {
                violation("%s: JPEG is %ux%u, the deck expects %ux%u after rotating %u", w, iw, ih, ew, eh,
                          emu->m->rotate);
            }
            return;
        }
        p += 2u + be16(d + p + 2);
    }
    violation("%s: JPEG has no SOF0 segment before the scan", w);
}

/* Six-key chunks carry no length, so the BMP's own size field says where the image ends. */
static void check_bmp(const char *w, deck_image_t *im, uint16_t ew, uint16_t eh)
{
    const uint8_t *d = im->data;
    if (im->len < 54 || d[0] != 'B' || d[1] != 'M') {
        violation("%s: not a BMP (no 'BM' header)", w);
        return;
    }
    uint32_t size = db_get_u32(d + 2);
    if (size < 54 || size > im->len) {
        violation("%s: BMP size field %u, but %zu bytes arrived", w, size, im->len);
        return;
    }
    for (size_t i = size; i < im->len; i++) {
        if (d[i]) {
            violation("%s: byte %zu after the BMP's end is not zero padding", w, i);
            break;
        }
    }
    im->len = size;
    int32_t bw = (int32_t)db_get_u32(d + 18), bh = (int32_t)db_get_u32(d + 22);
    uint16_t bpp = db_get_u16(d + 28);
    if (db_get_u32(d + 14) < 40) {
        violation("%s: BMP info header of %u bytes, expected BITMAPINFOHEADER (40)", w, db_get_u32(d + 14));
    }
    if (bw != ew || (bh < 0 ? -bh : bh) != eh) {
        violation("%s: BMP is %dx%d, the deck expects %ux%u", w, bw, bh, ew, eh);
    }
    if (bpp != 24) {
        violation("%s: BMP has %u bits per pixel, expected 24", w, bpp);
    } else if (db_get_u32(d + 10) + (uint32_t)ew * eh * 3 > size) {
        violation("%s: BMP pixel data is cut short", w);
    }
}

static void finish(void)
{
    char w[80];
    what(w, sizeof(w), emu->x.cmd, emu->x.key, emu->x.win);
    deck_image_t im = {.cmd = emu->x.cmd,
                       .key = emu->x.key,
                       .win = emu->x.win,
                       .data = emu->x.data,
                       .len = emu->x.len,
                       .first_report = emu->x.first_report,
                       .report_count = emu->nreports - emu->x.first_report};
    const deck_model_t *m = emu->m;
    uint16_t ew, eh;
    switch (emu->x.cmd) {
    case 0x08:
        rotated(m->lcd_w, m->lcd_h, &ew, &eh);
        break;
    case 0x0b:
        rotated(m->win_w, m->win_h, &ew, &eh);
        break;
    case 0x0c:
        rotated(emu->x.win.w, emu->x.win.h, &ew, &eh);
        break;
    default:
        rotated(m->key_px, m->key_px, &ew, &eh);
        break;
    }
    if (sixkey()) {
        check_bmp(w, &im, ew, eh);
    } else {
        check_jpeg(w, &im, ew, eh);
    }
    emu->x.active = false;
    emu->x.data = NULL;
    if (deck_emu_sink) {
        deck_emu_sink((int)(emu - decks), &im);
        free(im.data);
    } else if (emu->nimages < MAX_IMAGES) {
        emu->images[emu->nimages++] = im;
    } else {
        violation("more than %d images without a clear", MAX_IMAGES);
        free(im.data);
    }
}

static void feed(uint8_t cmd, uint8_t key, db_rect_t win, uint16_t chunk, bool last, const uint8_t *data,
                 size_t len)
{
    char w[80];
    what(w, sizeof(w), cmd, key, win);
    if (chunk == 0) {
        if (emu->x.active) {
            violation("%s: starts while the previous image still waits for its last chunk (next %u)", w,
                      emu->x.next);
        }
        free(emu->x.data);
        emu->x.active = true;
        emu->x.cmd = cmd;
        emu->x.key = key;
        emu->x.win = win;
        emu->x.next = 0;
        emu->x.data = NULL;
        emu->x.len = 0;
        emu->x.first_report = emu->nreports - 1;
    } else if (!emu->x.active) {
        violation("%s: chunk %u arrives without a chunk 0", w, chunk);
        return;
    }
    if (cmd != emu->x.cmd || key != emu->x.key || !db_rect_eq(win, emu->x.win)) {
        violation("%s: chunk %u changes command, key or rect in the middle of a transfer", w, chunk);
    }
    if (chunk != emu->x.next) {
        violation("%s: chunk index %u, expected %u", w, chunk, emu->x.next);
    }
    emu->x.next = (uint16_t)(chunk + 1);
    uint8_t *grown = realloc(emu->x.data, emu->x.len + len);
    if (!grown) {
        abort();
    }
    emu->x.data = grown;
    memcpy(emu->x.data + emu->x.len, data, len);
    emu->x.len += len;
    if (last) {
        finish();
    }
}

static void gen2_output(const uint8_t *r)
{
    const deck_model_t *m = emu->m;
    uint8_t cmd = r[1];
    if (m->family == DECK_PEDAL) {
        violation("output report 02 %02x: the Pedal has no display", cmd);
        return;
    }
    bool window = m->win_w != 0;
    if (cmd != 0x07 && !(cmd == 0x08 && m->lcd_w) && !(window && (cmd == 0x0b || cmd == 0x0c))) {
        violation("output command 0x%02x is not one %s supports", cmd, m->name);
        return;
    }
    db_rect_t win = {0};
    uint8_t key = 0, done;
    uint16_t chunk, size;
    size_t hdr;
    if (cmd == 0x0c) {
        win = (db_rect_t){db_get_u16(r + 2), db_get_u16(r + 4), db_get_u16(r + 6), db_get_u16(r + 8)};
        done = r[10];
        chunk = db_get_u16(r + 11);
        size = db_get_u16(r + 13);
        hdr = 16;
        if (r[15]) {
            violation("partial window chunk %u: reserved byte 15 is 0x%02x", chunk, r[15]);
        }
        if (!win.w || !win.h || win.x + win.w > m->win_w || win.y + win.h > m->win_h) {
            violation("partial window rect %u,%u %ux%u is not inside the %ux%u window", win.x, win.y, win.w,
                      win.h, m->win_w, m->win_h);
        }
    } else {
        key = r[2];
        done = r[3];
        size = db_get_u16(r + 4);
        chunk = db_get_u16(r + 6);
        hdr = 8;
        if (cmd == 0x07 && key >= m->cols * m->rows) {
            violation("key image for key %u, the %s has %u image keys", key, m->name, m->cols * m->rows);
            return;
        }
        if (cmd != 0x07 && key) {
            violation("command 0x%02x chunk %u: reserved byte 2 is 0x%02x", cmd, chunk, key);
        }
    }
    char w[80];
    what(w, sizeof(w), cmd, key, win);
    if (done > 1) {
        violation("%s chunk %u: done flag 0x%02x, expected 0 or 1", w, chunk, done);
    }
    if (size == 0 || size > OUT_LEN - hdr) {
        violation("%s chunk %u: size field %u, expected 1..%zu", w, chunk, size, OUT_LEN - hdr);
        size = (uint16_t)(size ? OUT_LEN - hdr : 0);
    }
    for (size_t i = hdr + size; i < OUT_LEN; i++) {
        if (r[i]) {
            violation("%s chunk %u: byte %zu after the %u data bytes is not zero padding", w, chunk, i, size);
            break;
        }
    }
    feed(cmd, key, win, chunk, done != 0, r + hdr, size);
}

static void sixkey_output(const uint8_t *r)
{
    if (r[1] != 0x01) {
        violation("output command 0x%02x is not one the six-key decks know", r[1]);
        return;
    }
    uint8_t chunk = r[2], done = r[4], k = r[5];
    if (k < 1 || k > 6) {
        violation("key image chunk %u: key byte %u, expected key index plus one (1..6)", chunk, k);
        return;
    }
    char w[80];
    what(w, sizeof(w), 0x01, (uint8_t)(k - 1), (db_rect_t){0});
    if (r[3]) {
        violation("%s chunk %u: reserved byte 3 is 0x%02x", w, chunk, r[3]);
    }
    if (done > 1) {
        violation("%s chunk %u: last-chunk flag 0x%02x, expected 0 or 1", w, chunk, done);
    }
    for (size_t i = 6; i < 16; i++) {
        if (r[i]) {
            violation("%s chunk %u: reserved header byte %zu is 0x%02x", w, chunk, i, r[i]);
            break;
        }
    }
    feed(0x01, (uint8_t)(k - 1), (db_rect_t){0}, chunk, done != 0, r + 16, OUT_LEN - 16);
}

static db_err_t emu_write(const db_hid_io_t *io, const uint8_t *report, size_t len)
{
    emu = (emu_t *)io; /* a driver talks to the deck its io belongs to */
    if (!emu->m) {
        return DB_ERR_IO;
    }
    if (len != OUT_LEN) {
        violation("output report of %zu bytes, expected %d", len, OUT_LEN);
    }
    if (deck_emu_sink) {
        emu->nreports = 0;
    }
    if (emu->nreports == MAX_REPORTS) {
        violation("more than %d output reports without a clear", MAX_REPORTS);
        return DB_OK;
    }
    uint8_t *r = emu->reports[emu->nreports++];
    memset(r, 0, OUT_LEN);
    memcpy(r, report, len < OUT_LEN ? len : OUT_LEN);
    if (r[0] != 0x02) {
        violation("output report id 0x%02x, expected 0x02", r[0]);
        return DB_OK;
    }
    if (sixkey()) {
        sixkey_output(r);
    } else {
        gen2_output(r);
    }
    return DB_OK;
}

static db_err_t emu_flush(const db_hid_io_t *io)
{
    emu = (emu_t *)io;
    return DB_OK;
}

static bool gen2_get(uint8_t *a)
{
    const deck_model_t *m = emu->m;
    switch (a[0]) {
    case 0x08:
        a[1] = m->rows;
        a[2] = m->cols;
        db_put_u16(a + 3, m->key_px);
        db_put_u16(a + 5, m->key_px);
        db_put_u16(a + 7, m->lcd_w);
        db_put_u16(a + 9, m->lcd_h);
        db_put_u16(a + 0x11, m->win_w);
        db_put_u16(a + 0x13, m->win_h);
        return true;
    case 0x04:
    case 0x05:
    case 0x07:
        a[1] = 0x0c;
        db_put_u32(a + 2, 0x1234abcd);
        memcpy(a + 6, gen2_fw, 8);
        return true;
    case 0x06:
        a[1] = (uint8_t)strlen(emu->serial);
        memcpy(a + 2, emu->serial, strlen(emu->serial));
        return true;
    case 0x0a:
        a[1] = 4;
        return true;
    default:
        return false;
    }
}

static bool sixkey_get(uint8_t *a)
{
    switch (a[0]) {
    case 0xa0:
    case 0xa1:
    case 0xa2:
        memcpy(a + 5, sixkey_fw, sizeof(sixkey_fw) - 1);
        return true;
    case 0x03:
        memcpy(a + 5, emu->serial, strlen(emu->serial));
        return true;
    case 0xa3:
        a[1] = 4;
        return true;
    default:
        return false;
    }
}

static db_err_t emu_get_feature(const db_hid_io_t *io, uint8_t *buf, size_t len, size_t *out_len)
{
    emu = (emu_t *)io;
    if (!emu->m || len == 0) {
        violation("GET_FEATURE with an empty buffer");
        return DB_ERR_IO;
    }
    uint8_t a[FEATURE_LEN] = {buf[0]};
    if (!(sixkey() ? sixkey_get(a) : gen2_get(a))) {
        violation("GET_FEATURE 0x%02x: not a report the %s answers", buf[0], emu->m->name);
        return DB_ERR_IO;
    }
    size_t n = len < FEATURE_LEN ? len : FEATURE_LEN;
    memcpy(buf, a, n);
    *out_len = n;
    return DB_OK;
}

/* Returns the payload length after the two id bytes, or -1 for a setter the deck does not know. */
static int gen2_set(const uint8_t *b)
{
    const deck_model_t *m = emu->m;
    bool pedal = m->family == DECK_PEDAL;
    if (b[0] != 0x03) {
        return -1;
    }
    switch (b[1]) {
    case 0x02:
        return 0;
    case 0x08:
        if (b[2] > 100) {
            violation("brightness %u, expected 0..100", b[2]);
        }
        emu->brightness = b[2];
        return 1;
    case 0x0d:
        return 4;
    case 0x05:
        return pedal ? -1 : 3;
    case 0x06:
        if (pedal) {
            return -1;
        }
        if (b[2] >= input_keys()) {
            violation("fill key color for key %u, the %s has %u keys", b[2], m->name, input_keys());
        }
        return 4;
    case 0x13:
        return m->family == DECK_XL ? 1 : -1;
    default:
        return -1;
    }
}

static int sixkey_set(const uint8_t *b)
{
    if (b[0] == 0x05 && b[1] == 0x55) {
        if (b[2] != 0xaa || b[3] != 0xd1 || b[4] != 0x01) {
            violation("brightness report 05 55 %02x %02x %02x, expected 05 55 aa d1 01", b[2], b[3], b[4]);
        }
        if (b[5] > 100) {
            violation("brightness %u, expected 0..100", b[5]);
        }
        emu->brightness = b[5];
        return 4;
    }
    if (b[0] == 0x0b && b[1] == 0x63) {
        if (b[2]) {
            violation("show logo 0b 63 %02x: only 0b 63 00 shows the logo (02 resets the deck)", b[2]);
        }
        return 1;
    }
    if (b[0] == 0x0b && b[1] == 0xa2) {
        return 4;
    }
    return -1;
}

static db_err_t emu_set_feature(const db_hid_io_t *io, const uint8_t *buf, size_t len)
{
    emu = (emu_t *)io;
    if (!emu->m || len < 2) {
        violation("SET_FEATURE of %zu bytes", len);
        return DB_ERR_IO;
    }
    if (len != FEATURE_LEN) {
        violation("SET_FEATURE %02x %02x of %zu bytes, expected %d", buf[0], buf[1], len, FEATURE_LEN);
    }
    uint8_t b[FEATURE_LEN] = {0};
    memcpy(b, buf, len < FEATURE_LEN ? len : FEATURE_LEN);
    int plen = sixkey() ? sixkey_set(b) : gen2_set(b);
    if (plen < 0) {
        violation("SET_FEATURE %02x %02x: not a setter the %s knows", b[0], b[1], emu->m->name);
        return DB_ERR_IO;
    }
    for (size_t i = 2 + (size_t)plen; i < FEATURE_LEN; i++) {
        if (b[i]) {
            violation("SET_FEATURE %02x %02x: byte %zu is not zero padding", b[0], b[1], i);
            break;
        }
    }
    return DB_OK;
}

static const db_hid_io_t io = {.write = emu_write,
                               .flush = emu_flush,
                               .get_feature = emu_get_feature,
                               .set_feature = emu_set_feature,
                               .out_report_size = OUT_LEN};

void deck_emu_clear(void)
{
    for (size_t i = 0; i < emu->nimages; i++) {
        free(emu->images[i].data);
    }
    free(emu->x.data);
    emu->x.data = NULL;
    emu->x.active = false;
    emu->nimages = 0;
    emu->nreports = 0;
    emu->nviol = 0;
    emu->brightness = -1;
}

void deck_emu_select(int deck)
{
    emu = &decks[deck];
}

bool deck_emu_start(uint16_t pid)
{
    deck_emu_clear();
    memset(emu->keys, 0, sizeof(emu->keys));
    memset(emu->enc_down, 0, sizeof(emu->enc_down));
    emu->m = deck_model(pid);
    emu->io = io;
    if (!emu->m) {
        return false;
    }
    /* each deck its own serial: deck 0 keeps the base one */
    snprintf(emu->serial, sizeof(emu->serial), "%s", sixkey() ? sixkey_serial : gen2_serial);
    emu->serial[strlen(emu->serial) - 1] = (char)(emu->serial[strlen(emu->serial) - 1] + (emu - decks));
    return true;
}

const db_hid_io_t *deck_emu_io(void)
{
    return &emu->io;
}

void deck_emu_check_idle(void)
{
    if (emu->x.active) {
        char w[80];
        what(w, sizeof(w), emu->x.cmd, emu->x.key, emu->x.win);
        violation("%s: transfer never sent its last chunk (stopped before chunk %u)", w, emu->x.next);
    }
}

size_t deck_emu_violation_count(void)
{
    return emu->nviol;
}

const char *deck_emu_violation(size_t i)
{
    return i < MAX_VIOLATIONS ? emu->violations[i] : "(more violations not stored)";
}

size_t deck_emu_image_count(void)
{
    return emu->nimages;
}

const deck_image_t *deck_emu_image(size_t i)
{
    return &emu->images[i];
}

const uint8_t *deck_emu_report(size_t i)
{
    return emu->reports[i];
}

int deck_emu_brightness(void)
{
    return emu->brightness;
}

const char *deck_emu_serial(void)
{
    return emu->serial;
}

const char *deck_emu_firmware(void)
{
    return sixkey() ? sixkey_fw : gen2_fw;
}

static size_t gen2_input(uint8_t type, uint16_t len, uint8_t *out, size_t cap)
{
    if (cap < INPUT_LEN) {
        return 0;
    }
    memset(out, 0, INPUT_LEN);
    out[0] = 0x01;
    out[1] = type;
    db_put_u16(out + 2, len);
    return INPUT_LEN;
}

size_t deck_emu_key(uint8_t key, bool pressed, uint8_t *out, size_t cap)
{
    unsigned n = input_keys();
    if (key >= n) {
        return 0;
    }
    emu->keys[key] = pressed;
    if (sixkey()) {
        if (cap < SIXKEY_INPUT_LEN) {
            return 0;
        }
        memset(out, 0, SIXKEY_INPUT_LEN);
        out[0] = 0x01;
        memcpy(out + 1, emu->keys, n);
        return SIXKEY_INPUT_LEN;
    }
    size_t len = gen2_input(0x00, (uint16_t)n, out, cap);
    if (len) {
        memcpy(out + 4, emu->keys, n);
    }
    return len;
}

size_t deck_emu_encoder_press(uint8_t enc, bool pressed, uint8_t *out, size_t cap)
{
    uint8_t n = emu->m->encoders;
    if (enc >= n) {
        return 0;
    }
    emu->enc_down[enc] = pressed;
    size_t len = gen2_input(0x03, (uint16_t)(n + 1), out, cap);
    if (len) {
        out[4] = 0x00;
        memcpy(out + 5, emu->enc_down, n);
    }
    return len;
}

size_t deck_emu_encoder_turn(uint8_t enc, int8_t ticks, uint8_t *out, size_t cap)
{
    uint8_t n = emu->m->encoders;
    if (enc >= n) {
        return 0;
    }
    size_t len = gen2_input(0x03, (uint16_t)(n + 1), out, cap);
    if (len) {
        out[4] = 0x01;
        out[5 + enc] = (uint8_t)ticks;
    }
    return len;
}

size_t deck_emu_touch(uint8_t kind, uint16_t x, uint16_t y, uint16_t x2, uint16_t y2, uint8_t *out,
                      size_t cap)
{
    if (!emu->m->touch || kind < 1 || kind > 3) {
        return 0;
    }
    uint16_t plen = kind == 3 ? 0x0e : kind == 1 ? emu->m->tap_len : 0x0a;
    size_t len = gen2_input(0x02, plen, out, cap);
    if (len) {
        out[4] = kind;
        db_put_u16(out + 6, x);
        db_put_u16(out + 8, y);
        if (kind == 3) {
            db_put_u16(out + 10, x2);
            db_put_u16(out + 12, y2);
        }
    }
    return len;
}
