#include <stdlib.h>
#include <string.h>
#include <unity.h>

#include "db_screen.h"

static db_screen_t sc;

void setUp(void)
{
    db_screen_init(&sc, realloc, free);
}

void tearDown(void)
{
    db_screen_clear(&sc);
    free(sc.pending.data);
}

static void put(db_rect_t r)
{
    db_screen_put(&sc, 0, r, malloc(4), 4);
}

static void test_new_image_drops_what_it_covers(void)
{
    put((db_rect_t){0, 380, 800, 100});
    put((db_rect_t){0, 380, 200, 100});
    put((db_rect_t){13, 12, 120, 120});
    TEST_ASSERT_EQUAL_INT(3, sc.count); /* the small strip part covers nothing */
    put((db_rect_t){0, 380, 800, 100});
    TEST_ASSERT_EQUAL_INT(2, sc.count); /* the full strip covers both strip images */
    TEST_ASSERT_EQUAL_UINT16(13, sc.shots[0].rect.x);
    uint32_t older = sc.shots[1].id;
    put((db_rect_t){0, 0, 800, 480});
    TEST_ASSERT_EQUAL_INT(1, sc.count);
    TEST_ASSERT_GREATER_THAN_UINT32(older, sc.shots[0].id); /* a new image gets a new id */
}

static void test_keeps_a_bounded_list(void)
{
    for (uint16_t i = 0; i < DB_SCREEN_MAX + 5; i++) {
        put((db_rect_t){i, 0, 1, 1});
    }
    TEST_ASSERT_EQUAL_INT(DB_SCREEN_MAX, sc.count);
    TEST_ASSERT_EQUAL_UINT16(5, sc.shots[0].rect.x); /* the oldest went first */
}

static void test_drops_an_image_with_missing_pieces(void)
{
    uint8_t piece[10] = {0};
    db_chunk_t c = {.rect = {0, 0, 10, 10}, .part = 0, .data = piece, .len = sizeof(piece)};
    TEST_ASSERT_EQUAL_INT(DB_OK, db_screen_feed(&sc, &c));
    c.part = 2;
    c.last = true;
    TEST_ASSERT_EQUAL_INT(DB_ERR_PROTO, db_screen_feed(&sc, &c));
    TEST_ASSERT_EQUAL_INT(0, sc.count);
    c.part = 0; /* the next image starts clean */
    TEST_ASSERT_EQUAL_INT(DB_OK, db_screen_feed(&sc, &c));
    TEST_ASSERT_EQUAL_INT(1, sc.count);
    TEST_ASSERT_EQUAL_size_t(10, sc.shots[0].len);
}

static void test_an_image_that_never_ends_is_bounded(void)
{
    static uint8_t piece[64 * 1024];
    db_chunk_t c = {.rect = {0, 0, 10, 10}, .data = piece, .len = sizeof(piece)};
    db_err_t err = DB_OK;
    for (uint16_t part = 0; part < 100 && err == DB_OK; part++) { /* a host that never sends the last piece */
        c.part = part;
        err = db_screen_feed(&sc, &c);
    }
    TEST_ASSERT_EQUAL_INT(DB_ERR_NOMEM, err);
    TEST_ASSERT_NULL(sc.pending.data); /* and its memory is given back */
}

static void test_empty_image_is_dropped(void)
{
    uint8_t piece[10] = {0};
    db_chunk_t c = {.rect = {0, 0, 10, 10}, .part = 0, .data = piece, .len = sizeof(piece)};
    db_screen_feed(&sc, &c);
    c.len = 0;
    c.last = true; /* a host restarts the image with an empty last piece */
    TEST_ASSERT_EQUAL_INT(DB_ERR_PROTO, db_screen_feed(&sc, &c));
    TEST_ASSERT_EQUAL_INT(0, sc.count);
}

static void test_an_empty_first_piece_is_taken(void)
{
    uint8_t piece[10] = {1};
    db_chunk_t c = {.rect = {0, 0, 10, 10}, .part = 0, .data = piece, .len = 0};
    TEST_ASSERT_EQUAL_INT(DB_OK, db_screen_feed(&sc, &c));
    c.part = 1;
    c.len = sizeof(piece);
    c.last = true;
    TEST_ASSERT_EQUAL_INT(DB_OK, db_screen_feed(&sc, &c));
    TEST_ASSERT_EQUAL_INT(1, sc.count);
}

/* A dock host picks the strip rect: an image placed off the picture must not write past it. */
static void test_blit_stays_inside_the_picture(void)
{
    enum { W = 8, H = 4 };
    uint8_t *dst = malloc(W * H * 3);
    uint8_t src[16 * 16 * 3];
    memset(src, 0xab, sizeof(src));
    TEST_ASSERT_EQUAL_INT(DB_ERR_PROTO, db_screen_blit(dst, W, H, W, H - 1, src, 16 * 3, 16, 16));
    TEST_ASSERT_EQUAL_INT(DB_ERR_PROTO, db_screen_blit(dst, W, H, 0, H, src, 16 * 3, 16, 16));
    memset(dst, 0, W * H * 3);
    TEST_ASSERT_EQUAL_INT(DB_OK, db_screen_blit(dst, W, H, W - 1, H - 1, src, 16 * 3, 16, 16));
    TEST_ASSERT_EQUAL_UINT8(0xab, dst[W * H * 3 - 1]); /* cut at the edge */
    TEST_ASSERT_EQUAL_UINT8(0, dst[W * H * 3 - 4]);
    free(dst);
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_new_image_drops_what_it_covers);
    RUN_TEST(test_keeps_a_bounded_list);
    RUN_TEST(test_drops_an_image_with_missing_pieces);
    RUN_TEST(test_empty_image_is_dropped);
    RUN_TEST(test_an_empty_first_piece_is_taken);
    RUN_TEST(test_an_image_that_never_ends_is_bounded);
    RUN_TEST(test_blit_stays_inside_the_picture);
    return UNITY_END();
}
