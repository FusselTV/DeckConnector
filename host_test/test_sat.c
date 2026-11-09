#include <stdlib.h>
#include <string.h>
#include <unity.h>
#include <zlib.h>

#include "db_driver.h"
#include "db_sat.h"

/* The ROM's miniz on the chip; zlib reads the same zlib streams. */
size_t tinfl_decompress_mem_to_mem(void *out, size_t out_len, const void *src, size_t src_len, int flags)
{
    (void)flags;
    uLongf n = out_len;
    return uncompress(out, &n, src, src_len) == Z_OK ? (size_t)n : (size_t)-1;
}

static db_err_t no_write(const uint8_t *r, size_t len)
{
    (void)r;
    (void)len;
    return DB_OK;
}

static db_err_t no_flush(void)
{
    return DB_OK;
}

static db_err_t feature(uint8_t *buf, size_t len, size_t *out_len)
{
    memset(buf, 0, len);
    *out_len = len;
    return DB_OK;
}

static db_err_t set_feature(const uint8_t *buf, size_t len)
{
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
    RUN_TEST(test_png_decodes_to_bgr);
    RUN_TEST(test_png_refuses_a_first_chunk_bigger_than_the_picture);
    RUN_TEST(test_host_of_every_satellite_address_form);
    return UNITY_END();
}
