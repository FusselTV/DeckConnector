#include <string.h>

#include "db_image.h"
#include "unity.h"

void setUp(void)
{
}
void tearDown(void)
{
}

#define W 4
#define H 2

static uint8_t px[W * H * 3];

static void fill(void)
{
    for (size_t i = 0; i < sizeof(px); i++) {
        px[i] = (uint8_t)i;
    }
}

static const uint8_t *at(const uint8_t *img, uint16_t w, uint16_t x, uint16_t y)
{
    return img + ((size_t)y * w + x) * 3;
}

static void test_quarter_turns_move_corners_and_undo(void)
{
    fill();
    uint8_t out[sizeof(px)];
    uint8_t back[sizeof(px)];
    db_display_t cw = {.rotate = 90};
    uint16_t ow = 0;
    uint16_t oh = 0;
    db_image_upload_size(&cw, W, H, &ow, &oh);
    TEST_ASSERT_EQUAL_UINT16(H, ow);
    TEST_ASSERT_EQUAL_UINT16(W, oh);
    db_image_orient(&cw, px, W, H, out, false);
    TEST_ASSERT_EQUAL_MEMORY(at(px, W, 0, 0), at(out, ow, H - 1, 0), 3); /* top left goes top right */
    TEST_ASSERT_EQUAL_MEMORY(at(px, W, W - 1, H - 1), at(out, ow, 0, W - 1), 3);
    db_image_orient(&cw, out, ow, oh, back, true);
    TEST_ASSERT_EQUAL_MEMORY(px, back, sizeof(px));

    db_display_t ccw = {.rotate = 270};
    db_image_orient(&ccw, px, W, H, out, false);
    TEST_ASSERT_EQUAL_MEMORY(at(px, W, 0, 0), at(out, ow, 0, W - 1), 3); /* top left goes bottom left */
    db_image_orient(&ccw, out, ow, oh, back, true);
    TEST_ASSERT_EQUAL_MEMORY(px, back, sizeof(px));
}

static void test_both_flips_are_a_half_turn(void)
{
    fill();
    uint8_t flipped[sizeof(px)];
    uint8_t turned[sizeof(px)];
    db_image_orient(&(db_display_t){.flip_x = true, .flip_y = true}, px, W, H, flipped, false);
    db_image_orient(&(db_display_t){.rotate = 180}, px, W, H, turned, false);
    TEST_ASSERT_EQUAL_MEMORY(turned, flipped, sizeof(px));
    TEST_ASSERT_EQUAL_MEMORY(at(px, W, 0, 0), at(flipped, W, W - 1, H - 1), 3);
}

static void test_bmp_round_trip(void)
{
    fill();
    uint8_t bmp[256];
    TEST_ASSERT_LESS_OR_EQUAL(sizeof(bmp), db_bmp_size(W, H));
    db_bmp_write(px, W, H, bmp);
    TEST_ASSERT_EQUAL_UINT8('B', bmp[0]);
    TEST_ASSERT_EQUAL_UINT32(db_bmp_size(W, H), db_get_u32(bmp + 2));
    TEST_ASSERT_EQUAL_UINT16(24, db_get_u16(bmp + 28));
    TEST_ASSERT_EQUAL_MEMORY(at(px, W, 0, H - 1), bmp + 54, 3); /* bottom row first */
    uint8_t back[sizeof(px)];
    TEST_ASSERT_EQUAL_INT(DB_OK, db_bmp_read(bmp, db_bmp_size(W, H), W, H, back));
    TEST_ASSERT_EQUAL_MEMORY(px, back, sizeof(px));
    TEST_ASSERT_EQUAL_INT(DB_ERR_PROTO, db_bmp_read(bmp, db_bmp_size(W, H), H, W, back));
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_quarter_turns_move_corners_and_undo);
    RUN_TEST(test_both_flips_are_a_half_turn);
    RUN_TEST(test_bmp_round_trip);
    return UNITY_END();
}
