/*
 * Elgato six-key decks (Mini family): the older HID protocol with BMP images.
 * Protocol: https://docs.elgato.com/streamdeck/hid/mini/
 */
#include <string.h>

#include "drivers.h"

#define ELGATO_VID  0x0fd9
#define REPORT_LEN  1024
#define HEADER_LEN  16
#define FEATURE_LEN 32
#define COLS        3
#define ROWS        2
#define KEYS        (COLS * ROWS)
#define KEY_PX      80

static const struct {
    uint16_t pid;
    const char *name;
} models[] = {
    {0x0063, "Stream Deck Mini"},
    {0x0090, "Stream Deck Mini"},
    {0x00b3, "Stream Deck Mini"},
    {0x00b8, "Stream Deck 6 Module"},
};

static const char *model_name(uint16_t pid)
{
    for (size_t i = 0; i < sizeof(models) / sizeof(models[0]); i++) {
        if (models[i].pid == pid) {
            return models[i].name;
        }
    }
    return NULL;
}

static bool match(uint16_t vid, uint16_t pid)
{
    return vid == ELGATO_VID && model_name(pid) != NULL;
}

/* Feature report strings: [id, 4 bytes, text...]. */
static void read_feature_string(db_dev_t *dev, uint8_t id, char *out, size_t cap)
{
    uint8_t buf[FEATURE_LEN] = {id};
    size_t n = 0;
    out[0] = '\0';
    if (dev->io->get_feature(buf, sizeof(buf), &n) != DB_OK || n <= 5) {
        return;
    }
    size_t len = strnlen((const char *)buf + 5, n - 5);
    if (len >= cap) {
        len = cap - 1;
    }
    memcpy(out, buf + 5, len);
    out[len] = '\0';
}

static db_err_t open_dev(db_dev_t *dev, uint16_t vid, uint16_t pid)
{
    (void)vid;
    const char *name = model_name(pid);
    if (!name) {
        return DB_ERR_UNSUPPORTED;
    }
    memset(dev->state, 0, sizeof(dev->state));
    db_device_info_t *info = &dev->info;
    *info = (db_device_info_t){.vendor = "Elgato",
                               .model = name,
                               .vid = ELGATO_VID,
                               .pid = pid,
                               .key_count = KEYS,
                               .display_count = 1};
    /* Each key image transposed (pixel x,y from row x, column y): turned 90 degrees clockwise and mirrored.
     * Elgato's page says only "rotate".
     */
    db_display_t *d = &info->displays[0];
    *d = (db_display_t){.width = COLS * KEY_PX,
                        .height = ROWS * KEY_PX,
                        .format = DB_FORMAT_BMP,
                        .rotate = 90,
                        .flip_x = true};
    for (uint8_t i = 0; i < KEYS; i++) {
        d->targets[d->target_count++] = (db_target_t){
            .kind = DB_TARGET_KEY,
            .index = i,
            .rect = {(uint16_t)(i % COLS * KEY_PX), (uint16_t)(i / COLS * KEY_PX), KEY_PX, KEY_PX}};
    }
    /* What the Elgato dock reports for these decks: 2 rows, 3 columns, 80x80 keys, a 320x240 screen. */
    static const uint8_t unit[] = {ROWS, COLS, 0, KEY_PX, 0, KEY_PX, 0, 0x40, 0x01, 0xf0, 0x00};
    memcpy(info->unit_info, unit, sizeof(unit));
    read_feature_string(dev, 0x03, info->serial, sizeof(info->serial));
    read_feature_string(dev, 0xa1, info->firmware, sizeof(info->firmware));
    return DB_OK;
}

/* [0x02, 0x01, chunk, 0, last, key + 1, 0 x 10, data]: no length field, the last chunk is padded. */
static db_err_t write_image(db_dev_t *dev, const db_target_t *t, db_rect_t rect, const uint8_t *data,
                            size_t len)
{
    (void)rect;
    uint8_t rep[REPORT_LEN];
    size_t room = REPORT_LEN - HEADER_LEN;
    for (size_t off = 0, part = 0; off < len; off += room, part++) {
        size_t n = len - off < room ? len - off : room;
        memset(rep, 0, sizeof(rep));
        rep[0] = 0x02;
        rep[1] = 0x01;
        rep[2] = (uint8_t)part;
        rep[4] = off + n >= len;
        rep[5] = (uint8_t)(t->index + 1);
        memcpy(rep + HEADER_LEN, data + off, n);
        db_err_t err = dev->io->write(rep, sizeof(rep));
        if (err != DB_OK) {
            return err;
        }
    }
    return dev->io->flush();
}

static bool parse_output(const db_dev_t *dev, const uint8_t *r, size_t len, db_chunk_t *out)
{
    if (len < REPORT_LEN || r[0] != 0x02 || r[1] != 0x01 || r[5] < 1 || r[5] > KEYS) {
        return false;
    }
    *out = (db_chunk_t){.display = 0,
                        .rect = dev->info.displays[0].targets[r[5] - 1].rect,
                        .part = r[2],
                        .last = r[4] != 0,
                        .data = r + HEADER_LEN,
                        .len = REPORT_LEN - HEADER_LEN};
    return true;
}

/* Input report: [0x01, one byte per key]. */
static size_t parse_input(db_dev_t *dev, const uint8_t *r, size_t len, uint64_t t, db_input_t *out,
                          size_t max)
{
    if (len < 1 + KEYS || r[0] != 0x01) {
        return 0;
    }
    size_t n = 0;
    for (uint8_t i = 0; i < KEYS; i++) {
        uint8_t v = r[1 + i] ? 1 : 0;
        if (v != dev->state[i]) {
            dev->state[i] = v;
            if (n < max) {
                out[n++] =
                    (db_input_t){.kind = v ? DB_INPUT_KEY_DOWN : DB_INPUT_KEY_UP, .index = i, .t_us = t};
            }
        }
    }
    return n;
}

static size_t build_input(const db_dev_t *dev, const db_input_t *ev, uint8_t *r, size_t cap)
{
    if (cap < 65 || (ev->kind != DB_INPUT_KEY_DOWN && ev->kind != DB_INPUT_KEY_UP) || ev->index >= KEYS) {
        return 0;
    }
    memset(r, 0, 65);
    r[0] = 0x01;
    memcpy(r + 1, dev->state, KEYS);
    r[1 + ev->index] = ev->kind == DB_INPUT_KEY_DOWN;
    return 65;
}

static db_err_t set_brightness(db_dev_t *dev, uint8_t pct)
{
    uint8_t buf[FEATURE_LEN] = {0x05, 0x55, 0xaa, 0xd1, 0x01, pct > 100 ? 100 : pct};
    return dev->io->set_feature(buf, sizeof(buf));
}

const db_driver_t db_driver_elgato_gen1 = {
    .name = "elgato-gen1",
    .match = match,
    .open = open_dev,
    .write_image = write_image,
    .parse_input = parse_input,
    .set_brightness = set_brightness,
    .parse_output = parse_output,
    .build_input = build_input,
};
