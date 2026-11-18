#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unity.h>
#include <zlib.h>

#include "db_driver.h"
#include "db_proto.h"
#include "db_sat.h"
#include "mirabox_ref.h"

/* The ROM's miniz on the chip; zlib reads the same zlib streams. */
size_t tinfl_decompress_mem_to_mem(void *out, size_t out_len, const void *src, size_t src_len, int flags)
{
    (void)flags;
    uLongf n = out_len;
    return uncompress(out, &n, src, src_len) == Z_OK ? (size_t)n : (size_t)-1;
}

static db_err_t no_write(const db_hid_io_t *io, const uint8_t *r, size_t len)
{
    (void)io;
    (void)r;
    (void)len;
    return DB_OK;
}

static db_err_t no_flush(const db_hid_io_t *io)
{
    (void)io;
    return DB_OK;
}

static db_err_t feature(const db_hid_io_t *io, uint8_t *buf, size_t len, size_t *out_len)
{
    (void)io;
    memset(buf, 0, len);
    *out_len = len;
    return DB_OK;
}

static db_err_t set_feature(const db_hid_io_t *io, const uint8_t *buf, size_t len)
{
    (void)io;
    (void)buf;
    (void)len;
    return DB_OK;
}

static const db_hid_io_t io = {no_write, no_flush, feature, set_feature, 1024};

static db_device_info_t open_deck(uint16_t pid)
{
    db_dev_t dev = {.io = &io, .driver = db_driver_find(0x0fd9, pid)};
    TEST_ASSERT_EQUAL_INT(DB_OK, dev.driver->open(&dev, 0x0fd9, pid));
    return dev.info;
}

static db_dev_t open_usb(uint16_t vid, uint16_t pid)
{
    db_dev_t dev = {.io = &io, .driver = db_driver_find(vid, pid)};
    TEST_ASSERT_EQUAL_INT(DB_OK, dev.driver->open(&dev, vid, pid));
    return dev;
}

void setUp(void)
{
}

void tearDown(void)
{
}

static void test_parse_a_line_as_companion_writes_it(void)
{
    char line[] = "KEY-STATE DEVICEID=\"probe 1\" CONTROLID=\"0/2\" PRESSED=0 TEXT=\"a \\\"b\\\" c\\\\d\" "
                  "BITMAP=\"data:image/png;base64,iVB=\"  OK";
    db_sat_msg_t m;
    db_sat_parse(line, &m);
    TEST_ASSERT_EQUAL_STRING("KEY-STATE", m.cmd);
    TEST_ASSERT_EQUAL_INT(6, m.count);
    TEST_ASSERT_EQUAL_STRING("probe 1", db_sat_get(&m, "DEVICEID"));
    TEST_ASSERT_EQUAL_STRING("0/2", db_sat_get(&m, "CONTROLID"));
    TEST_ASSERT_EQUAL_STRING("0", db_sat_get(&m, "PRESSED"));
    TEST_ASSERT_EQUAL_STRING("a \"b\" c\\d", db_sat_get(&m, "TEXT"));
    TEST_ASSERT_EQUAL_STRING("data:image/png;base64,iVB=", db_sat_get(&m, "BITMAP"));
    TEST_ASSERT_EQUAL_STRING("", db_sat_get(&m, "OK"));
    TEST_ASSERT_NULL(db_sat_get(&m, "MISSING"));

    char bare[] = "PING";
    db_sat_parse(bare, &m);
    TEST_ASSERT_EQUAL_STRING("PING", m.cmd);
    TEST_ASSERT_EQUAL_INT(0, m.count);
}

/* The layout of Companion's deck plugin (surface-schema.ts with @elgato-stream-deck/core's model). */
static void test_stream_deck_plus_layout_matches_companion(void)
{
    db_device_info_t dev = open_deck(0x0084);
    db_sat_control_t c[DB_SAT_MAX_CONTROLS];
    size_t n = db_sat_controls(&dev, c, DB_SAT_MAX_CONTROLS);
    TEST_ASSERT_EQUAL_UINT(16, n);
    const db_sat_control_t *k = db_sat_find(c, n, "1/3");
    TEST_ASSERT_EQUAL_INT(DB_SAT_KEY, k->kind);
    TEST_ASSERT_EQUAL_UINT8(7, k->index);
    TEST_ASSERT_EQUAL_UINT16(670, k->rect.x);
    const db_sat_control_t *lcd = db_sat_find(c, n, "2/1");
    TEST_ASSERT_EQUAL_INT(DB_SAT_LCD, lcd->kind);
    TEST_ASSERT_TRUE(db_rect_eq((db_rect_t){200, 380, 200, 100}, lcd->rect));
    TEST_ASSERT_EQUAL_PTR(lcd, db_sat_lcd_at(c, n, 399));
    TEST_ASSERT_EQUAL_INT(DB_SAT_ENCODER, db_sat_find(c, n, "3/3")->kind);
    TEST_ASSERT_NULL(db_sat_find(c, n, "4/0"));
    TEST_ASSERT_NULL(db_sat_find(c, n, "3"));

    char buf[2048];
    size_t len = db_sat_manifest(c, n, buf, sizeof(buf));
    TEST_ASSERT_GREATER_THAN(0, len);
    TEST_ASSERT_NOT_NULL(strstr(buf, "\"stylePresets\":{\"default\":{\"bitmap\":{\"w\":120,\"h\":120}},"
                                     "\"lcd\":{\"bitmap\":{\"w\":200,\"h\":100}},\"none\":{}}"));
    TEST_ASSERT_NOT_NULL(strstr(buf, "\"0/0\":{\"row\":0,\"column\":0}"));
    TEST_ASSERT_NOT_NULL(strstr(buf, "\"2/3\":{\"row\":2,\"column\":3,\"stylePreset\":\"lcd\"}"));
    TEST_ASSERT_NOT_NULL(strstr(buf, "\"3/0\":{\"row\":3,\"column\":0,\"stylePreset\":\"none\"}"));
    TEST_ASSERT_EQUAL_UINT(0, db_sat_manifest(c, n, buf, 100));
}

static void test_decks_without_a_strip_are_a_grid_of_keys(void)
{
    const struct {
        uint16_t pid;
        size_t keys;
        const char *last;
    } decks[] = {{0x0080, 15, "2/4"}, {0x008f, 32, "3/7"}, {0x0063, 6, "1/2"}};
    for (size_t d = 0; d < sizeof(decks) / sizeof(decks[0]); d++) {
        db_device_info_t dev = open_deck(decks[d].pid);
        db_sat_control_t c[DB_SAT_MAX_CONTROLS];
        size_t n = db_sat_controls(&dev, c, DB_SAT_MAX_CONTROLS);
        TEST_ASSERT_EQUAL_UINT(decks[d].keys, n);
        TEST_ASSERT_EQUAL_UINT8(decks[d].keys - 1, db_sat_find(c, n, decks[d].last)->index);
    }
}

/* Companion's plugin: the Neo's sensors left and right of its info bar, the Pedal one row without pictures.
 */
static void test_buttons_without_a_picture(void)
{
    db_device_info_t neo = open_deck(0x009a);
    db_sat_control_t c[DB_SAT_MAX_CONTROLS];
    size_t n = db_sat_controls(&neo, c, DB_SAT_MAX_CONTROLS);
    TEST_ASSERT_EQUAL_UINT(11, n);
    TEST_ASSERT_EQUAL_UINT8(8, db_sat_find(c, n, "2/0")->index);
    TEST_ASSERT_EQUAL_UINT8(9, db_sat_find(c, n, "2/3")->index);
    TEST_ASSERT_EQUAL_INT(DB_SAT_LCD, db_sat_find(c, n, "2/1")->kind);
    char buf[2048];
    TEST_ASSERT_GREATER_THAN(0, db_sat_manifest(c, n, buf, sizeof(buf)));
    TEST_ASSERT_NOT_NULL(strstr(buf, "\"2/0\":{\"row\":2,\"column\":0,\"stylePreset\":\"none\"}"));
    TEST_ASSERT_NOT_NULL(strstr(buf, "\"1/3\":{\"row\":1,\"column\":3}"));

    db_device_info_t pedal = open_deck(0x0086);
    n = db_sat_controls(&pedal, c, DB_SAT_MAX_CONTROLS);
    TEST_ASSERT_EQUAL_UINT(3, n);
    TEST_ASSERT_EQUAL_UINT8(2, db_sat_find(c, n, "0/2")->index);
    TEST_ASSERT_GREATER_THAN(0, db_sat_manifest(c, n, buf, sizeof(buf)));
    TEST_ASSERT_NOT_NULL(strstr(buf, "\"stylePresets\":{\"default\":{},\"none\":{}}"));
}

/* 36 keys, 6 LCD parts and 6 encoders fill DB_SAT_MAX_CONTROLS exactly. */
static void test_plus_xl_fits(void)
{
    db_device_info_t xl = open_deck(0x00c6);
    db_sat_control_t c[DB_SAT_MAX_CONTROLS];
    size_t n = db_sat_controls(&xl, c, DB_SAT_MAX_CONTROLS);
    TEST_ASSERT_EQUAL_UINT(48, n);
    TEST_ASSERT_EQUAL_INT(DB_SAT_LCD, db_sat_find(c, n, "4/5")->kind);
    TEST_ASSERT_EQUAL_INT(DB_SAT_ENCODER, db_sat_find(c, n, "5/5")->kind);
    char buf[2048];
    TEST_ASSERT_GREATER_THAN(0, db_sat_manifest(c, n, buf, sizeof(buf)));
}

/* Companion gets the places of its own plugin and a preset for each key size. */
static void test_satellite_layout_of_the_n4(void)
{
    db_dev_t dev = open_usb(0x6602, 0x1001);
    db_sat_control_t c[DB_SAT_MAX_CONTROLS];
    size_t n = db_sat_controls(&dev.info, c, DB_SAT_MAX_CONTROLS);
    TEST_ASSERT_EQUAL_size_t(14 + 5, n);
    const db_sat_control_t *swipe = db_sat_find(c, n, "2/4");
    TEST_ASSERT_NOT_NULL(swipe);
    TEST_ASSERT_EQUAL_INT(DB_SAT_ENCODER, swipe->kind);
    TEST_ASSERT_EQUAL_UINT8(4, swipe->index);
    TEST_ASSERT_EQUAL_UINT16(176, db_sat_find(c, n, "2/3")->rect.w);
    TEST_ASSERT_EQUAL_INT(DB_SAT_ENCODER, db_sat_find(c, n, "3/0")->kind);
    char buf[4096];
    TEST_ASSERT_GREATER_THAN(0, db_sat_manifest(c, n, buf, sizeof(buf)));
    TEST_ASSERT_NOT_NULL(strstr(buf, "\"default\":{\"bitmap\":{\"w\":112,\"h\":112}}"));
    TEST_ASSERT_NOT_NULL(strstr(buf, "\"176x124\":{\"bitmap\":{\"w\":176,\"h\":124}}"));
    TEST_ASSERT_NOT_NULL(strstr(buf, "\"2/0\":{\"row\":2,\"column\":0,\"stylePreset\":\"176x124\"}"));
    TEST_ASSERT_NOT_NULL(strstr(buf, "\"2/4\":{\"row\":2,\"column\":4,\"stylePreset\":\"none\"}"));
    const char *preset = strstr(buf, "\"176x124\":{\"bitmap\"");
    TEST_ASSERT_NULL(strstr(preset + 1, "\"176x124\":{\"bitmap\""));
}

static void test_xl_side_buttons_have_no_picture(void)
{
    db_dev_t dev = open_usb(0x5548, 0x1028);
    TEST_ASSERT_EQUAL_UINT8(38, dev.info.key_count);
    TEST_ASSERT_EQUAL_UINT8(32, db_device_image_keys(&dev.info));
    db_sat_control_t c[DB_SAT_MAX_CONTROLS];
    size_t n = db_sat_controls(&dev.info, c, DB_SAT_MAX_CONTROLS);
    TEST_ASSERT_EQUAL_UINT16(0, db_sat_find(c, n, "0/0")->rect.w);
    TEST_ASSERT_EQUAL_UINT16(80, db_sat_find(c, n, "0/1")->rect.w);
    TEST_ASSERT_EQUAL_UINT16(0, db_sat_find(c, n, "2/9")->rect.w);
}

/* Every Elgato layout and device description as before Companion's surface prefix moved into the driver:
 * FNV-1a of the manifest and the device JSON. */
static void test_elgato_decks_are_described_as_before(void)
{
    static const struct {
        uint16_t pid;
        uint32_t fnv;
    } decks[] = {
        {0x0063, 0x2081d3e7}, {0x0090, 0x7d78151a}, {0x00b3, 0x3f52d4e2}, {0x00b8, 0x82ae91e3},
        {0x0084, 0xdae4efb7}, {0x00c6, 0xce4fa05b}, {0x0080, 0xc8eb4f9d}, {0x006d, 0xfb21902a},
        {0x00a5, 0x131de650}, {0x006c, 0xe91fcb97}, {0x009a, 0x14eefb1b}, {0x008f, 0xe38a8370},
        {0x00ba, 0x18ff6d22}, {0x00b9, 0x8dee7904}, {0x00e4, 0x88e3828c}, {0x0086, 0xd3619394},
    };
    for (size_t d = 0; d < sizeof(decks) / sizeof(decks[0]); d++) {
        db_device_info_t dev = open_deck(decks[d].pid);
        TEST_ASSERT_TRUE(dev.dock);
        TEST_ASSERT_EQUAL_STRING("streamdeck", dev.surface);
        db_sat_control_t c[DB_SAT_MAX_CONTROLS];
        size_t n = db_sat_controls(&dev, c, DB_SAT_MAX_CONTROLS);
        static char buf[8192];
        size_t len = db_sat_manifest(c, n, buf, sizeof(buf));
        db_proto_device_json(&dev, buf + len, sizeof(buf) - len);
        uint32_t h = 2166136261u;
        for (const char *p = buf; *p; p++) {
            h = (h ^ (uint8_t)*p) * 16777619u;
        }
        TEST_ASSERT_EQUAL_HEX32(decks[d].fnv, h);
    }
}

/* sat.c sends the manifest from 2048 bytes. */
static void test_every_mirabox_layout_fits(void)
{
    for (size_t m = 0; m < sizeof(ref_models) / sizeof(ref_models[0]); m++) {
        for (uint8_t u = 0; u < ref_models[m].id_count; u++) {
            uint16_t vid = ref_models[m].ids[u].vid;
            uint16_t pid = ref_models[m].ids[u].pid;
            if (!db_driver_find(vid, pid)) {
                continue;
            }
            db_dev_t dev = open_usb(vid, pid);
            db_sat_control_t c[DB_SAT_MAX_CONTROLS];
            size_t n = db_sat_controls(&dev.info, c, DB_SAT_MAX_CONTROLS);
            TEST_ASSERT_EQUAL_size_t(dev.info.place_count, n);
            char buf[2048];
            TEST_ASSERT_GREATER_THAN_MESSAGE(0, db_sat_manifest(c, n, buf, sizeof(buf)), ref_models[m].name);
        }
    }
}

static void put_be32(uint8_t *p, uint32_t v)
{
    p[0] = (uint8_t)(v >> 24);
    p[1] = (uint8_t)(v >> 16);
    p[2] = (uint8_t)(v >> 8);
    p[3] = (uint8_t)v;
}

static size_t chunk(uint8_t *out, const char *type, const uint8_t *data, size_t len)
{
    put_be32(out, (uint32_t)len);
    memcpy(out + 4, type, 4);
    if (len) {
        memcpy(out + 8, data, len);
    }
    memset(out + 8 + len, 0, 4); /* the CRC is not checked */
    return len + 12;
}

static uint8_t predict(int type, int left, int up, int corner)
{
    int p = left + up - corner;
    int pa = abs(p - left);
    int pb = abs(p - up);
    int pc = abs(p - corner);
    int paeth = pa <= pb && pa <= pc ? left : (pb <= pc ? up : corner);
    int v[] = {0, left, up, (left + up) / 2, paeth};
    return (uint8_t)v[type];
}

/* Every filter type, one per row, and the compressed data split over two IDAT chunks. */
static size_t make_png(uint8_t *out, const uint8_t *rgb, uint16_t w, uint16_t h, size_t bpp)
{
    size_t stride = 1 + bpp * w;
    uint8_t *raw = malloc(h * stride);
    for (size_t y = 0; y < h; y++) {
        int type = (int)(y % 5);
        raw[y * stride] = (uint8_t)type;
        for (size_t i = 0; i < bpp * w; i++) {
            const uint8_t *row = rgb + y * bpp * w;
            const uint8_t *prev = y ? row - bpp * w : NULL;
            int left = i >= bpp ? row[i - bpp] : 0;
            int up = prev ? prev[i] : 0;
            int corner = prev && i >= bpp ? prev[i - bpp] : 0;
            raw[y * stride + 1 + i] = (uint8_t)(row[i] - predict(type, left, up, corner));
        }
    }
    uLongf zlen = compressBound(h * stride);
    uint8_t *z = malloc(zlen);
    TEST_ASSERT_EQUAL_INT(Z_OK, compress(z, &zlen, raw, h * stride));
    static const uint8_t sig[8] = {0x89, 'P', 'N', 'G', '\r', '\n', 0x1a, '\n'};
    memcpy(out, sig, 8);
    uint8_t ihdr[13] = {0};
    put_be32(ihdr, w);
    put_be32(ihdr + 4, h);
    ihdr[8] = 8;
    ihdr[9] = bpp == 4 ? 6 : 2;
    size_t n = 8 + chunk(out + 8, "IHDR", ihdr, 13);
    n += chunk(out + n, "IDAT", z, zlen / 2);
    n += chunk(out + n, "IDAT", z + zlen / 2, zlen - zlen / 2);
    n += chunk(out + n, "IEND", NULL, 0);
    free(raw);
    free(z);
    return n;
}

static void test_png_decodes_to_bgr(void)
{
    enum { W = 20, H = 11 };
    for (size_t bpp = 3; bpp <= 4; bpp++) {
        uint8_t px[W * H * 4];
        for (size_t i = 0; i < sizeof(px); i++) {
            px[i] = (uint8_t)(i * 7 + i / 13);
        }
        uint8_t png[4096];
        size_t len = make_png(png, px, W, H, bpp);
        uint8_t bgr[W * H * 3];
        uint8_t work[H * (1 + 4 * W)];
        TEST_ASSERT_EQUAL_INT(DB_OK, db_png_decode(png, len, W, H, bgr, work, sizeof(work)));
        for (size_t p = 0; p < W * H; p++) {
            TEST_ASSERT_EQUAL_UINT8(px[p * bpp + 2], bgr[p * 3]);
            TEST_ASSERT_EQUAL_UINT8(px[p * bpp], bgr[p * 3 + 2]);
        }
        TEST_ASSERT_EQUAL_INT(DB_ERR_PROTO, db_png_decode(png, len, W + 1, H, bgr, work, sizeof(work)));
        TEST_ASSERT_EQUAL_INT(DB_ERR_NOMEM, db_png_decode(png, len, W, H, bgr, work, 10));
        TEST_ASSERT_EQUAL_INT(DB_ERR_PROTO, db_png_decode(png, len - 20, W, H, bgr, work, sizeof(work)));
    }
}

/* A peer chooses the chunk sizes: a first IDAT bigger than the picture must not be copied anywhere. */
static void test_png_refuses_a_first_chunk_bigger_than_the_picture(void)
{
    enum { W = 20, H = 11 };
    size_t big = W * H * 3 * 2;
    uint8_t *png = calloc(1, big + 200);
    static const uint8_t sig[8] = {0x89, 'P', 'N', 'G', '\r', '\n', 0x1a, '\n'};
    memcpy(png, sig, 8);
    uint8_t ihdr[13] = {0};
    put_be32(ihdr, W);
    put_be32(ihdr + 4, H);
    ihdr[8] = 8;
    ihdr[9] = 2;
    size_t n = 8 + chunk(png + 8, "IHDR", ihdr, 13);
    uint8_t *zeros = calloc(1, big);
    n += chunk(png + n, "IDAT", zeros, big);
    n += chunk(png + n, "IDAT", zeros, 4);
    free(zeros);
    uint8_t *bgr = malloc(W * H * 3);
    uint8_t work[H * (1 + 3 * W)];
    TEST_ASSERT_EQUAL_INT(DB_ERR_NOMEM, db_png_decode(png, n, W, H, bgr, work, sizeof(work)));
    free(bgr);
    free(png);
}

static void test_add_device_asks_to_draw_the_lock_itself(void)
{
    char line[256];
    size_t n = db_sat_add_device(line, sizeof(line), "streamdeck", "A1", "Stream Deck +", "e30=", true, true);
    TEST_ASSERT_EQUAL_size_t(strlen(line), n);
    TEST_ASSERT_EQUAL_STRING(
        "ADD-DEVICE DEVICEID=\"A1\" SERIAL=\"streamdeck:A1\" PRODUCT_NAME=\"Stream Deck +\" "
        "LAYOUT_MANIFEST=\"e30=\" BITMAP_FORMAT=png PINCODE_LOCK=FULL "
        "CAN_CHANGE_PAGE=\"Horizontal Swipe Changes Page\"\n",
        line);
    db_sat_msg_t m;
    line[n - 1] = '\0';
    db_sat_parse(line, &m);
    TEST_ASSERT_EQUAL_STRING("FULL", db_sat_get(&m, "PINCODE_LOCK"));
    TEST_ASSERT_EQUAL_size_t(0, db_sat_add_device(line, 40, "streamdeck", "A1", "Stream Deck +", "e30=", true, true));
}

static bool locked_state(db_sat_lock_t *l, const char *text)
{
    char line[128];
    snprintf(line, sizeof(line), "%s", text);
    db_sat_msg_t m;
    db_sat_parse(line, &m);
    return db_sat_lock_update(l, &m);
}

static void test_lock_counts_digits_and_tells_a_wrong_pin(void)
{
    db_sat_lock_t l = {0};
    TEST_ASSERT_FALSE(locked_state(&l, "LOCKED-STATE DEVICEID=\"A1\" LOCKED=1 CHARACTER_COUNT=0 ROTATION=0"));
    TEST_ASSERT_TRUE(l.locked);
    TEST_ASSERT_EQUAL_UINT8(0, l.count);
    TEST_ASSERT_FALSE(locked_state(&l, "LOCKED-STATE DEVICEID=\"A1\" LOCKED=true CHARACTER_COUNT=1"));
    TEST_ASSERT_FALSE(locked_state(&l, "LOCKED-STATE DEVICEID=\"A1\" LOCKED=1 CHARACTER_COUNT=3"));
    TEST_ASSERT_EQUAL_UINT8(3, l.count);
    TEST_ASSERT_TRUE(locked_state(&l, "LOCKED-STATE DEVICEID=\"A1\" LOCKED=1 CHARACTER_COUNT=0"));
    TEST_ASSERT_TRUE(l.locked);
    TEST_ASSERT_FALSE(locked_state(&l, "LOCKED-STATE DEVICEID=\"A1\" LOCKED=1 CHARACTER_COUNT=0"));
    locked_state(&l, "LOCKED-STATE DEVICEID=\"A1\" LOCKED=1 CHARACTER_COUNT=2");
    TEST_ASSERT_FALSE(locked_state(&l, "LOCKED-STATE DEVICEID=\"A1\" LOCKED=0 CHARACTER_COUNT=0"));
    TEST_ASSERT_FALSE(l.locked);
    TEST_ASSERT_FALSE(locked_state(&l, "LOCKED-STATE DEVICEID=\"A1\" LOCKED=false CHARACTER_COUNT=4"));
    TEST_ASSERT_FALSE(l.locked);
    TEST_ASSERT_EQUAL_UINT8(0, l.count);
    TEST_ASSERT_FALSE(locked_state(&l, "LOCKED-STATE DEVICEID=\"A1\" CHARACTER_COUNT=4"));
    TEST_ASSERT_FALSE(l.locked);
    locked_state(&l, "LOCKED-STATE DEVICEID=\"A1\" LOCKED=1 CHARACTER_COUNT=999");
    TEST_ASSERT_EQUAL_UINT8(255, l.count);
}

static void test_only_answered_digits_count_for_a_wrong_pin(void)
{
    db_sat_lock_t l = {0};
    locked_state(&l, "LOCKED-STATE DEVICEID=\"A1\" LOCKED=1 CHARACTER_COUNT=0 ROTATION=-90");
    TEST_ASSERT_EQUAL_INT16(-90, l.rotation);
    l.pending++; /* the first digit goes out */
    /* a redraw sent before Companion read the digit: count 0 is not a wrong PIN */
    TEST_ASSERT_FALSE(locked_state(&l, "LOCKED-STATE DEVICEID=\"A1\" LOCKED=1 CHARACTER_COUNT=0"));
    TEST_ASSERT_EQUAL_UINT8(1, l.pending);
    TEST_ASSERT_FALSE(locked_state(&l, "LOCKED-STATE DEVICEID=\"A1\" LOCKED=1 CHARACTER_COUNT=1"));
    TEST_ASSERT_FALSE(locked_state(&l, "PINCODE-KEY OK DEVICEID=\"A1\""));
    TEST_ASSERT_EQUAL_UINT8(1, l.count);
    TEST_ASSERT_EQUAL_UINT8(0, l.pending);
    l.pending++; /* refused: the dot goes again */
    TEST_ASSERT_FALSE(locked_state(&l, "PINCODE-KEY ERROR DEVICEID=\"A1\" MESSAGE=\"Invalid KEY\""));
    TEST_ASSERT_EQUAL_UINT8(0, l.pending);
    TEST_ASSERT_FALSE(locked_state(&l, "PINCODE-KEY OK DEVICEID=\"A1\""));
    TEST_ASSERT_EQUAL_UINT8(0, l.pending);
    TEST_ASSERT_TRUE(locked_state(&l, "LOCKED-STATE DEVICEID=\"A1\" LOCKED=1 CHARACTER_COUNT=0"));
    l.pending = 2;
    locked_state(&l, "LOCKED-STATE DEVICEID=\"A1\" LOCKED=0 CHARACTER_COUNT=0 ROTATION=45");
    TEST_ASSERT_EQUAL_UINT8(0, l.pending);
    TEST_ASSERT_EQUAL_INT16(0, l.rotation);
}

static bool pass(db_sat_held_t *h, db_input_kind_t kind, uint8_t index, bool ours)
{
    db_input_t ev = {.kind = kind, .index = index};
    return db_sat_pass(h, &ev, ours);
}

static void test_a_release_goes_only_after_its_press(void)
{
    db_sat_held_t h = {0};
    TEST_ASSERT_FALSE(pass(&h, DB_INPUT_KEY_DOWN, 3, false)); /* the last PIN digit */
    TEST_ASSERT_FALSE(pass(&h, DB_INPUT_KEY_UP, 3, true));    /* released once Companion draws again */
    TEST_ASSERT_TRUE(pass(&h, DB_INPUT_KEY_DOWN, 3, true));
    TEST_ASSERT_TRUE(pass(&h, DB_INPUT_KEY_UP, 3, true));
    TEST_ASSERT_FALSE(pass(&h, DB_INPUT_KEY_UP, 3, true));
    TEST_ASSERT_TRUE(pass(&h, DB_INPUT_KEY_DOWN, 40, true));
    TEST_ASSERT_FALSE(pass(&h, DB_INPUT_KEY_UP, 40, false)); /* the gate closed meanwhile */
    TEST_ASSERT_FALSE(pass(&h, DB_INPUT_KEY_UP, 40, true));
    TEST_ASSERT_FALSE(pass(&h, DB_INPUT_ENCODER_DOWN, 2, false));
    TEST_ASSERT_FALSE(pass(&h, DB_INPUT_ENCODER_UP, 2, true));
    TEST_ASSERT_TRUE(pass(&h, DB_INPUT_ENCODER_DOWN, 2, true));
    TEST_ASSERT_FALSE(pass(&h, DB_INPUT_KEY_UP, 2, true)); /* key 2 is not encoder 2 */
    TEST_ASSERT_TRUE(pass(&h, DB_INPUT_ENCODER_UP, 2, true));
    TEST_ASSERT_TRUE(pass(&h, DB_INPUT_ENCODER_TURN, 1, true));
    TEST_ASSERT_FALSE(pass(&h, DB_INPUT_TOUCH_TAP, 0, false));
}

static bool keeps(db_sat_held_t *h, db_input_kind_t kind, uint8_t index, bool locked)
{
    db_input_t ev = {.kind = kind, .index = index};
    return db_sat_pad_keeps(h, &ev, locked);
}

static void test_the_pin_pad_keeps_its_presses_to_itself(void)
{
    db_sat_held_t h = {0};
    TEST_ASSERT_TRUE(keeps(&h, DB_INPUT_KEY_DOWN, 1, true));
    TEST_ASSERT_TRUE(keeps(&h, DB_INPUT_KEY_UP, 1, false)); /* the last digit, released after the unlock */
    TEST_ASSERT_FALSE(keeps(&h, DB_INPUT_KEY_UP, 1, false));
    TEST_ASSERT_FALSE(keeps(&h, DB_INPUT_KEY_DOWN, 2, false)); /* held when the lock came */
    TEST_ASSERT_FALSE(keeps(&h, DB_INPUT_KEY_UP, 2, true));
    TEST_ASSERT_TRUE(keeps(&h, DB_INPUT_ENCODER_DOWN, 3, true));
    TEST_ASSERT_FALSE(keeps(&h, DB_INPUT_KEY_UP, 3, false)); /* key 3 is not encoder 3 */
    TEST_ASSERT_TRUE(keeps(&h, DB_INPUT_ENCODER_UP, 3, false));
    TEST_ASSERT_TRUE(keeps(&h, DB_INPUT_ENCODER_TURN, 0, true));
    TEST_ASSERT_TRUE(keeps(&h, DB_INPUT_TOUCH_TAP, 0, true));
    TEST_ASSERT_FALSE(keeps(&h, DB_INPUT_ENCODER_TURN, 0, false));
    TEST_ASSERT_FALSE(keeps(&h, DB_INPUT_TOUCH_SWIPE, 0, false));
}

static void test_host_of_every_satellite_address_form(void)
{
    const char *cases[][2] = {
        {"192.168.1.10", "192.168.1.10"},
        {"192.168.1.10:16622", "192.168.1.10"},
        {"ws://192.168.1.10:16623", "192.168.1.10"},
        {"wss://companion.example.org/satellite", "companion.example.org"},
        {"", ""},
    };
    char host[64];
    for (size_t i = 0; i < sizeof(cases) / sizeof(cases[0]); i++) {
        db_sat_addr_host(cases[i][0], host, sizeof(host));
        TEST_ASSERT_EQUAL_STRING(cases[i][1], host);
    }
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_parse_a_line_as_companion_writes_it);
    RUN_TEST(test_stream_deck_plus_layout_matches_companion);
    RUN_TEST(test_decks_without_a_strip_are_a_grid_of_keys);
    RUN_TEST(test_buttons_without_a_picture);
    RUN_TEST(test_plus_xl_fits);
    RUN_TEST(test_satellite_layout_of_the_n4);
    RUN_TEST(test_xl_side_buttons_have_no_picture);
    RUN_TEST(test_elgato_decks_are_described_as_before);
    RUN_TEST(test_every_mirabox_layout_fits);
    RUN_TEST(test_png_decodes_to_bgr);
    RUN_TEST(test_png_refuses_a_first_chunk_bigger_than_the_picture);
    RUN_TEST(test_host_of_every_satellite_address_form);
    RUN_TEST(test_add_device_asks_to_draw_the_lock_itself);
    RUN_TEST(test_lock_counts_digits_and_tells_a_wrong_pin);
    RUN_TEST(test_only_answered_digits_count_for_a_wrong_pin);
    RUN_TEST(test_a_release_goes_only_after_its_press);
    RUN_TEST(test_the_pin_pad_keeps_its_presses_to_itself);
    return UNITY_END();
}
