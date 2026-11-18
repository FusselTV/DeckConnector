#include "mirabox_emu.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "mirabox_ref.h"

#define IN_LEN 512

typedef struct {
    db_hid_io_t io; /* first: the io callbacks find their deck by it */
    const ref_model_t *m;
    uint16_t vid;
    int brightness;
    uint8_t id;
    size_t want;
    size_t got;
    uint8_t *data;
} emu_t;

static emu_t decks[MIRABOX_EMU_DECKS];
void (*mirabox_emu_sink)(int deck, uint8_t id, const uint8_t *data, size_t len);

__attribute__((format(printf, 2, 3))) static void violation(const emu_t *e, const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    fprintf(stderr, "mirabox_emu %s: ", e->m->name);
    vfprintf(stderr, fmt, ap);
    fputc('\n', stderr);
    va_end(ap);
}

/* Degrees the picture is turned before upload, from Bitfocus' iconRotation (see test_mirabox.c) */
static int rotation(const emu_t *e)
{
    int r = e->m->rotation;
    return r == 270 ? (e->vid == 0x6602 ? 0 : 90) : r == 180 ? 180 : r == 90 ? 270 : r == -90 ? 90 : 0;
}

static const ref_out_t *out_of(const emu_t *e, uint8_t id)
{
    for (uint8_t i = 0; i < e->m->out_count; i++) {
        if (e->m->out[i].id == id) {
            return &e->m->out[i];
        }
    }
    return NULL;
}

static void check_jpeg(const emu_t *e, const ref_out_t *o)
{
    const uint8_t *d = e->data;
    size_t n = e->got;
    if (n < 4 || d[0] != 0xff || d[1] != 0xd8 || d[n - 2] != 0xff || d[n - 1] != 0xd9) {
        violation(e, "image for id 0x%02x is not a whole JPEG", o->id);
        return;
    }
    bool swap = rotation(e) % 180 == 90;
    for (size_t p = 2; p + 9 <= n && d[p] == 0xff;) {
        if (d[p + 1] == 0xc0) {
            unsigned h = d[p + 5] << 8 | d[p + 6], w = d[p + 7] << 8 | d[p + 8];
            unsigned ew = swap ? o->h : o->w, eh = swap ? o->w : o->h;
            if (w != ew || h != eh) {
                violation(e, "image for id 0x%02x is %ux%u, the deck takes %ux%u", o->id, w, h, ew, eh);
            }
            return;
        }
        p += 2u + (unsigned)(d[p + 2] << 8 | d[p + 3]);
    }
    violation(e, "image for id 0x%02x has no SOF0", o->id);
}

static db_err_t emu_write(const db_hid_io_t *io, const uint8_t *r, size_t len)
{
    emu_t *e = (emu_t *)io;
    if (len != e->m->packet) {
        violation(e, "output report of %zu bytes, the deck takes %u", len, e->m->packet);
    }
    if (e->data && e->got < e->want) {
        size_t n = e->want - e->got < len ? e->want - e->got : len;
        memcpy(e->data + e->got, r, n);
        e->got += n;
        return DB_OK;
    }
    if (memcmp(r, "CRT\0\0", 5) != 0) {
        violation(e, "output report without CRT header");
        return DB_OK;
    }
    const uint8_t *c = r + 5;
    if (memcmp(c, "BAT", 3) == 0) {
        if (e->data) {
            violation(e, "BAT while the last image waits for STP");
        }
        free(e->data);
        e->want = (size_t)c[3] << 24 | (size_t)c[4] << 16 | (size_t)c[5] << 8 | c[6];
        e->id = c[7];
        e->got = 0;
        e->data = malloc(e->want ? e->want : 1);
        if (!out_of(e, e->id)) {
            violation(e, "BAT for picture id 0x%02x, which the deck does not have", e->id);
        }
    } else if (memcmp(c, "STP", 3) == 0) {
        const ref_out_t *o = e->data ? out_of(e, e->id) : NULL;
        if (!e->data || e->got < e->want) {
            violation(e, "STP without a whole image");
        } else if (o) {
            check_jpeg(e, o);
            if (mirabox_emu_sink) {
                mirabox_emu_sink((int)(e - decks), e->id, e->data, e->got);
            }
        }
        free(e->data);
        e->data = NULL;
    } else if (memcmp(c, "LIG", 3) == 0) {
        e->brightness = c[5];
    } else if (memcmp(c, "DIS", 3) && memcmp(c, "CLE", 3) && memcmp(c, "LBLIG", 5) &&
               memcmp(c, "CONNECT", 7)) {
        violation(e, "unknown command %.3s", (const char *)c);
    }
    return DB_OK;
}

static db_err_t emu_flush(const db_hid_io_t *io)
{
    return DB_OK;
}

bool mirabox_emu_start(int deck, uint16_t vid, uint16_t pid)
{
    emu_t *e = &decks[deck];
    free(e->data);
    *e = (emu_t){.io = {.write = emu_write, .flush = emu_flush}, .vid = vid, .brightness = -1};
    for (size_t m = 0; m < sizeof(ref_models) / sizeof(ref_models[0]); m++) {
        for (uint8_t i = 0; i < ref_models[m].id_count; i++) {
            if (ref_models[m].ids[i].vid == vid && ref_models[m].ids[i].pid == pid) {
                e->m = &ref_models[m];
                e->io.out_report_size = e->m->packet;
                return true;
            }
        }
    }
    return false;
}

const char *mirabox_emu_name(int deck)
{
    return decks[deck].m->name;
}

const db_hid_io_t *mirabox_emu_io(int deck)
{
    return &decks[deck].io;
}

int mirabox_emu_brightness(int deck)
{
    return decks[deck].brightness;
}

size_t mirabox_emu_input(int deck, uint8_t row, uint8_t col, bool turn, int state, uint8_t *out, size_t cap)
{
    const ref_model_t *m = decks[deck].m;
    for (uint8_t i = 0; i < m->in_count && cap >= IN_LEN; i++) {
        const ref_in_t *in = &m->in[i];
        bool left = in->kind == REF_LEFT || in->kind == REF_SWIPE_LEFT;
        bool right = in->kind == REF_RIGHT || in->kind == REF_SWIPE_RIGHT;
        bool press = in->kind == REF_BUTTON || in->kind == REF_PUSH;
        if (in->row != row || in->col != col ||
            !(turn ? (state < 0 && left) || (state > 0 && right) : press)) {
            continue;
        }
        if (!turn && !state && in->kind == REF_PUSH) {
            return MIRABOX_EMU_NONE;
        }
        memset(out, 0, IN_LEN);
        memcpy(out, "ACK\0\0OK\0\0", 9);
        out[9] = in->id;
        out[10] = turn ? 0 : (uint8_t)state;
        return IN_LEN;
    }
    return 0;
}
