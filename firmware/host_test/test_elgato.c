#include <stdlib.h>
#include <string.h>
#include <unity.h>

#include "db_driver.h"
#include "db_proto.h"

/* Fake USB: records output reports, answers feature reports like a Stream Deck +. */
static uint8_t written[64][1024];
static int written_n;
static uint8_t feature_set[32];
static int flushes;

static db_err_t fake_write(const db_hid_io_t *io, const uint8_t *r, size_t len)
{
    (void)io;
    TEST_ASSERT_EQUAL_size_t(1024, len);
    memcpy(written[written_n++], r, len);
    return DB_OK;
}

static db_err_t fake_flush(const db_hid_io_t *io)
{
    (void)io;
    flushes++;
    return DB_OK;
}

static db_err_t fake_get_feature(const db_hid_io_t *io, uint8_t *buf, size_t len, size_t *out_len)
{
    (void)io;
    static const char serial[] = "A00SA0000AA0A0";
    static const char fw[] = "1.01.008";
    memset(buf + 1, 0, len - 1);
    if (buf[0] == 0x06) {
        buf[1] = sizeof(serial) - 1;
        memcpy(buf + 2, serial, sizeof(serial) - 1);
    } else if (buf[0] == 0x05) {
        buf[1] = sizeof(fw) - 1 + 4; /* 4 checksum bytes come first */
        memcpy(buf + 6, fw, sizeof(fw) - 1);
    } else if (buf[0] == 0x08) {
        static const uint8_t unit[] = {0x08, 0x02, 0x04, 0x78, 0x00, 0x78, 0x00, 0x20, 0x03, 0xe0, 0x01,
                                       0x04, 0x05, 0x20, 0x00, 0x04, 0xaa, 0x20, 0x03, 0x64, 0x00};
        memcpy(buf, unit, sizeof(unit));
    }
    *out_len = len;
    return DB_OK;
}

static db_err_t fake_set_feature(const db_hid_io_t *io, const uint8_t *buf, size_t len)
{
    (void)io;
    memcpy(feature_set, buf, len);
    return DB_OK;
}

static const db_hid_io_t io = {.write = fake_write,
                               .flush = fake_flush,
                               .get_feature = fake_get_feature,
                               .set_feature = fake_set_feature,
                               .out_report_size = 1024};
static db_dev_t dev;

void setUp(void)
{
    written_n = 0;
    flushes = 0;
    dev = (db_dev_t){.io = &io};
    dev.driver = db_driver_find(0x0fd9, 0x0084);
    TEST_ASSERT_NOT_NULL(dev.driver);
    TEST_ASSERT_EQUAL_INT(DB_OK, dev.driver->open(&dev, 0x0fd9, 0x0084));
}

void tearDown(void)
{
}

static void test_unknown_device_has_no_driver(void)
{
    TEST_ASSERT_NULL(db_driver_find(0x0fd9, 0x1234));
    TEST_ASSERT_NULL(db_driver_find(0x1234, 0x0084));
}

static void test_describes_stream_deck_plus(void)
{
    const db_device_info_t *i = &dev.info;
    TEST_ASSERT_EQUAL_STRING("Stream Deck +", i->model);
    TEST_ASSERT_EQUAL_STRING("A00SA0000AA0A0", i->serial);
    TEST_ASSERT_EQUAL_STRING("1.01.008", i->firmware);
    TEST_ASSERT_EQUAL_UINT8(8, i->key_count);
    TEST_ASSERT_EQUAL_UINT8(4, i->encoder_count);
    TEST_ASSERT_EQUAL_UINT8(10, i->displays[0].target_count); /* full + 8 keys + strip */
    TEST_ASSERT_NOT_NULL(db_device_find_target(i, 0, (db_rect_t){0, 0, 800, 480}));
    const db_target_t *k5 = db_device_find_target(i, 0, (db_rect_t){232, 172, 120, 120});
    TEST_ASSERT_EQUAL_UINT8(5, k5->index);
    TEST_ASSERT_EQUAL_INT(DB_TARGET_STRIP,
                          db_device_find_target(i, 0, (db_rect_t){200, 380, 200, 100})->kind);
    TEST_ASSERT_NULL(db_device_find_target(i, 0, (db_rect_t){0, 0, 100, 100}));
    TEST_ASSERT_NULL(db_device_find_target(i, 1, (db_rect_t){0, 0, 800, 480}));

    char json[2048];
    TEST_ASSERT_NOT_EQUAL(0, db_proto_device_json(i, json, sizeof(json)));
    TEST_ASSERT_NOT_NULL(
        strstr(json, "\"kind\":\"key\",\"index\":5,\"x\":232,\"y\":172,\"w\":120,\"h\":120"));
    TEST_ASSERT_NOT_NULL(
        strstr(json, "\"kind\":\"strip\",\"x\":0,\"y\":380,\"w\":800,\"h\":100,\"partial\":true"));
}

/* The deck's 0x08 answer inside 03 1c: rows and columns first, its gap bytes zero. */
static void test_unit_info_as_the_dock_keeps_it(void)
{
    static const uint8_t dock[] = {0x02, 0x04, 0x00, 0x78, 0x00, 0x78, 0x00, 0x20, 0x03, 0xe0, 0x01,
                                   0x04, 0x05, 0x20, 0x00, 0x04, 0x00, 0x20, 0x03, 0x64, 0x00};
    TEST_ASSERT_EQUAL_HEX8_ARRAY(dock, dev.info.unit_info, sizeof(dock));
}

static void test_splits_key_image_into_reports(void)
{
    uint8_t jpeg[2100];
    for (size_t i = 0; i < sizeof(jpeg); i++) {
        jpeg[i] = (uint8_t)i;
    }
    const db_target_t *t = db_device_find_target(&dev.info, 0, (db_rect_t){670, 172, 120, 120});
    TEST_ASSERT_EQUAL_INT(DB_OK, dev.driver->write_image(&dev, t, t->rect, jpeg, sizeof(jpeg)));
    TEST_ASSERT_EQUAL_INT(3, written_n);
    TEST_ASSERT_EQUAL_INT(1, flushes);
    const uint8_t first[8] = {0x02, 0x07, 7, 0, 0xf8, 0x03, 0, 0}; /* 1016 bytes, part 0 */
    TEST_ASSERT_EQUAL_HEX8_ARRAY(first, written[0], 8);
    TEST_ASSERT_EQUAL_HEX8(1, written[2][3]);                          /* last */
    TEST_ASSERT_EQUAL_UINT16(2100 - 2032, db_get_u16(written[2] + 4)); /* rest */
    TEST_ASSERT_EQUAL_UINT16(2, db_get_u16(written[2] + 6));
    TEST_ASSERT_EQUAL_HEX8_ARRAY(jpeg + 1016, written[1] + 8, 1016);
}

static void test_full_and_partial_strip_headers(void)
{
    uint8_t jpeg[10] = {0};
    const db_target_t *full = db_device_find_target(&dev.info, 0, (db_rect_t){0, 0, 800, 480});
    dev.driver->write_image(&dev, full, full->rect, jpeg, sizeof(jpeg));
    const uint8_t fh[8] = {0x02, 0x08, 0, 1, 10, 0, 0, 0};
    TEST_ASSERT_EQUAL_HEX8_ARRAY(fh, written[0], 8);

    db_rect_t quarter = {400, 380, 200, 100};
    const db_target_t *strip = db_device_find_target(&dev.info, 0, quarter);
    dev.driver->write_image(&dev, strip, quarter, jpeg, sizeof(jpeg));
    /* x and y are relative to the strip */
    const uint8_t sh[16] = {0x02, 0x0c, 0x90, 0x01, 0, 0, 200, 0, 100, 0, 1, 0, 0, 10, 0, 0};
    TEST_ASSERT_EQUAL_HEX8_ARRAY(sh, written[1], 16);
}

static void test_neo_info_bar_is_one_whole_picture(void)
{
    TEST_ASSERT_EQUAL_INT(DB_OK, dev.driver->open(&dev, 0x0fd9, 0x009a));
    db_rect_t bar = {0, 192, 248, 58};
    const db_target_t *t = db_device_find_target(&dev.info, 0, bar);
    TEST_ASSERT_NOT_NULL(t);
    TEST_ASSERT_FALSE(t->partial);
    uint8_t jpeg[1100];
    for (size_t i = 0; i < sizeof(jpeg); i++) {
        jpeg[i] = (uint8_t)i;
    }
    TEST_ASSERT_EQUAL_INT(DB_OK, dev.driver->write_image(&dev, t, bar, jpeg, sizeof(jpeg)));
    TEST_ASSERT_EQUAL_INT(2, written_n);
    const uint8_t first[8] = {0x02, 0x0b, 0, 0, 0xf8, 0x03, 0, 0}; /* 1016 bytes, part 0 */
    const uint8_t last[8] = {0x02, 0x0b, 0, 1, 84, 0, 1, 0};
    TEST_ASSERT_EQUAL_HEX8_ARRAY(first, written[0], 8);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(last, written[1], 8);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(jpeg + 1016, written[1] + 8, 84);
    db_chunk_t c;
    TEST_ASSERT_TRUE(dev.driver->parse_output(&dev, written[1], 1024, &c));
    TEST_ASSERT_TRUE(db_rect_eq(bar, c.rect));
    TEST_ASSERT_EQUAL_UINT16(1, c.part);
    TEST_ASSERT_TRUE(c.last);
    written[0][1] = 0x0c; /* the + strip's command means nothing to the Neo */
    TEST_ASSERT_FALSE(dev.driver->parse_output(&dev, written[0], 1024, &c));
}

static void round_trip(db_rect_t rect, size_t size)
{
    uint8_t *jpeg = malloc(size);
    for (size_t i = 0; i < size; i++) {
        jpeg[i] = (uint8_t)(i * 7);
    }
    written_n = 0;
    const db_target_t *t = db_device_find_target(&dev.info, 0, rect);
    TEST_ASSERT_EQUAL_INT(DB_OK, dev.driver->write_image(&dev, t, rect, jpeg, size));
    db_screen_t sc;
    db_screen_init(&sc, realloc, free);
    for (int i = 0; i < written_n; i++) {
        db_chunk_t c;
        TEST_ASSERT_TRUE(dev.driver->parse_output(&dev, written[i], 1024, &c));
        TEST_ASSERT_EQUAL_INT(DB_OK, db_screen_feed(&sc, &c));
    }
    TEST_ASSERT_EQUAL_INT(1, sc.count);
    TEST_ASSERT_TRUE(db_rect_eq(rect, sc.shots[0].rect));
    TEST_ASSERT_EQUAL_size_t(size, sc.shots[0].len);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(jpeg, sc.shots[0].data, size);
    db_screen_clear(&sc);
    free(sc.pending.data);
    free(jpeg);
}

static void test_reads_back_what_it_writes(void)
{
    round_trip((db_rect_t){232, 12, 120, 120}, 2100);  /* key 1 */
    round_trip((db_rect_t){400, 380, 200, 100}, 3000); /* part of the strip */
    round_trip((db_rect_t){0, 0, 800, 480}, 40000);    /* full screen */
    db_chunk_t c;
    uint8_t input[64] = {0x01, 0x00};
    TEST_ASSERT_FALSE(dev.driver->parse_output(&dev, input, sizeof(input), &c));
}

static size_t parse(const uint8_t *r, size_t len, db_input_t *out)
{
    return dev.driver->parse_input(&dev, r, len, 42, out, 8);
}

static void test_key_edges(void)
{
    uint8_t r[512] = {0x01, 0x00, 8, 0, 0, 1};
    db_input_t ev[8];
    TEST_ASSERT_EQUAL_size_t(1, parse(r, sizeof(r), ev));
    TEST_ASSERT_EQUAL_INT(DB_INPUT_KEY_DOWN, ev[0].kind);
    TEST_ASSERT_EQUAL_UINT8(1, ev[0].index);
    TEST_ASSERT_EQUAL_UINT64(42, ev[0].t_us);
    TEST_ASSERT_EQUAL_size_t(0, parse(r, sizeof(r), ev)); /* unchanged state */
    r[5] = 0;
    r[11] = 1;
    TEST_ASSERT_EQUAL_size_t(2, parse(r, sizeof(r), ev));
    TEST_ASSERT_EQUAL_INT(DB_INPUT_KEY_UP, ev[0].kind);
    TEST_ASSERT_EQUAL_INT(DB_INPUT_KEY_DOWN, ev[1].kind);
    TEST_ASSERT_EQUAL_UINT8(7, ev[1].index);
}

static void test_encoder_turn_and_press(void)
{
    uint8_t turn[512] = {0x01, 0x03, 5, 0, 0x01, 0, 0xfd, 0, 2};
    db_input_t ev[8];
    TEST_ASSERT_EQUAL_size_t(2, parse(turn, sizeof(turn), ev));
    TEST_ASSERT_EQUAL_INT(DB_INPUT_ENCODER_TURN, ev[0].kind);
    TEST_ASSERT_EQUAL_UINT8(1, ev[0].index);
    TEST_ASSERT_EQUAL_INT16(-3, ev[0].value);
    TEST_ASSERT_EQUAL_INT16(2, ev[1].value);

    uint8_t press[512] = {0x01, 0x03, 5, 0, 0x00, 0, 0, 1, 0};
    TEST_ASSERT_EQUAL_size_t(1, parse(press, sizeof(press), ev));
    TEST_ASSERT_EQUAL_INT(DB_INPUT_ENCODER_DOWN, ev[0].kind);
    TEST_ASSERT_EQUAL_UINT8(2, ev[0].index);
}

static void test_touch_in_display_coordinates(void)
{
    uint8_t tap[512] = {0x01, 0x02, 0x0a, 0, 0x01, 0, 59, 0, 75, 0};
    db_input_t ev[8];
    TEST_ASSERT_EQUAL_size_t(1, parse(tap, sizeof(tap), ev));
    TEST_ASSERT_EQUAL_INT(DB_INPUT_TOUCH_TAP, ev[0].kind);
    TEST_ASSERT_EQUAL_UINT16(59, ev[0].x);
    TEST_ASSERT_EQUAL_UINT16(380 + 75, ev[0].y);

    uint8_t swipe[512] = {0x01, 0x02, 0x0e, 0, 0x03, 0, 0xa5, 0x02, 67, 0, 0x6e, 0x02, 71, 0};
    TEST_ASSERT_EQUAL_size_t(1, parse(swipe, sizeof(swipe), ev));
    TEST_ASSERT_EQUAL_INT(DB_INPUT_TOUCH_SWIPE, ev[0].kind);
    TEST_ASSERT_EQUAL_UINT16(677, ev[0].x);
    TEST_ASSERT_EQUAL_UINT16(622, ev[0].x2);
    TEST_ASSERT_EQUAL_UINT16(380 + 71, ev[0].y2);
}

static db_input_t simulate(db_input_t ev)
{
    uint8_t r[64];
    size_t len = dev.driver->build_input(&dev, &ev, r, sizeof(r));
    TEST_ASSERT_NOT_EQUAL(0, len);
    db_input_t out[8];
    TEST_ASSERT_EQUAL_size_t(1, parse(r, len, out));
    return out[0];
}

static void test_builds_what_the_deck_would_send(void)
{
    db_input_t e = simulate((db_input_t){.kind = DB_INPUT_KEY_DOWN, .index = 2});
    TEST_ASSERT_EQUAL_INT(DB_INPUT_KEY_DOWN, e.kind);
    TEST_ASSERT_EQUAL_UINT8(2, e.index);
    e = simulate((db_input_t){.kind = DB_INPUT_KEY_DOWN, .index = 5}); /* key 2 stays down */
    TEST_ASSERT_EQUAL_UINT8(5, e.index);
    e = simulate((db_input_t){.kind = DB_INPUT_KEY_UP, .index = 2});
    TEST_ASSERT_EQUAL_INT(DB_INPUT_KEY_UP, e.kind);
    TEST_ASSERT_EQUAL_UINT8(2, e.index);
    e = simulate((db_input_t){.kind = DB_INPUT_ENCODER_TURN, .index = 3, .value = -2});
    TEST_ASSERT_EQUAL_INT(DB_INPUT_ENCODER_TURN, e.kind);
    TEST_ASSERT_EQUAL_INT16(-2, e.value);
    e = simulate((db_input_t){.kind = DB_INPUT_ENCODER_DOWN, .index = 1});
    TEST_ASSERT_EQUAL_INT(DB_INPUT_ENCODER_DOWN, e.kind);
    e = simulate((db_input_t){.kind = DB_INPUT_TOUCH_SWIPE, .x = 600, .y = 420, .x2 = 100, .y2 = 430});
    TEST_ASSERT_EQUAL_INT(DB_INPUT_TOUCH_SWIPE, e.kind);
    TEST_ASSERT_EQUAL_UINT16(600, e.x);
    TEST_ASSERT_EQUAL_UINT16(430, e.y2);
    uint8_t r[64];
    TEST_ASSERT_EQUAL_size_t(
        0, dev.driver->build_input(&dev, &(db_input_t){.kind = DB_INPUT_KEY_DOWN, .index = 8}, r, sizeof(r)));
}

/* db_deck keeps simulated presses on a copy of the device, as here: a test's held key must not turn a
 * person's next real report into a release, or swallow their press of the same key. */
static void test_a_simulated_hold_leaves_the_real_key_states_alone(void)
{
    db_dev_t real = dev;
    simulate((db_input_t){.kind = DB_INPUT_KEY_DOWN, .index = 3}); /* held, never released */
    uint8_t r[12] = {0x01, 0x00, 8, 0};
    r[4 + 3] = 1; /* the person presses key 3 too */
    db_input_t out[8];
    TEST_ASSERT_EQUAL_size_t(1, real.driver->parse_input(&real, r, sizeof(r), 42, out, 8));
    TEST_ASSERT_EQUAL_INT(DB_INPUT_KEY_DOWN, out[0].kind);
    TEST_ASSERT_EQUAL_UINT8(3, out[0].index);
}

static void test_ignores_garbage(void)
{
    uint8_t junk[3] = {0x01, 0x00, 0};
    uint8_t other[512] = {0x02, 0x00};
    db_input_t ev[8];
    TEST_ASSERT_EQUAL_size_t(0, parse(junk, sizeof(junk), ev));
    TEST_ASSERT_EQUAL_size_t(0, parse(other, sizeof(other), ev));
}

static void test_brightness_clamps(void)
{
    dev.driver->set_brightness(&dev, 150);
    TEST_ASSERT_EQUAL_HEX8(0x03, feature_set[0]);
    TEST_ASSERT_EQUAL_HEX8(0x08, feature_set[1]);
    TEST_ASSERT_EQUAL_UINT8(100, feature_set[2]);
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_unknown_device_has_no_driver);
    RUN_TEST(test_describes_stream_deck_plus);
    RUN_TEST(test_unit_info_as_the_dock_keeps_it);
    RUN_TEST(test_splits_key_image_into_reports);
    RUN_TEST(test_full_and_partial_strip_headers);
    RUN_TEST(test_neo_info_bar_is_one_whole_picture);
    RUN_TEST(test_reads_back_what_it_writes);
    RUN_TEST(test_key_edges);
    RUN_TEST(test_encoder_turn_and_press);
    RUN_TEST(test_touch_in_display_coordinates);
    RUN_TEST(test_builds_what_the_deck_would_send);
    RUN_TEST(test_a_simulated_hold_leaves_the_real_key_states_alone);
    RUN_TEST(test_ignores_garbage);
    RUN_TEST(test_brightness_clamps);
    return UNITY_END();
}
