#include <stdlib.h>
#include <unity.h>

#include "db_regions.h"

static db_regions_t q;
static uint32_t dropped[16];
static int dropped_n;

void setUp(void)
{
    db_regions_init(&q, 1000);
    dropped_n = 0;
}

void tearDown(void)
{
}

static void on_drop(db_region_t *r, void *arg)
{
    (void)arg;
    dropped[dropped_n++] = r->seq;
    free(r);
}

static db_region_t *region(uint32_t seq, uint8_t display, db_rect_t rect, size_t len)
{
    db_region_t *r = calloc(1, sizeof(*r));
    *r = (db_region_t){.seq = seq, .display = display, .rect = rect, .len = len};
    return r;
}

static void test_keeps_fifo_order(void)
{
    db_regions_put(&q, region(1, 0, (db_rect_t){0, 0, 10, 10}, 10), on_drop, NULL);
    db_regions_put(&q, region(2, 0, (db_rect_t){20, 0, 10, 10}, 10), on_drop, NULL);
    db_region_t *a = db_regions_take(&q);
    db_region_t *b = db_regions_take(&q);
    TEST_ASSERT_EQUAL_UINT32(1, a->seq);
    TEST_ASSERT_EQUAL_UINT32(2, b->seq);
    TEST_ASSERT_NULL(db_regions_take(&q));
    TEST_ASSERT_EQUAL_size_t(0, q.bytes);
    free(a);
    free(b);
}

static void test_newer_image_replaces_same_area(void)
{
    db_rect_t key = {13, 12, 120, 120};
    db_regions_put(&q, region(1, 0, key, 100), on_drop, NULL);
    db_regions_put(&q, region(2, 0, key, 100), on_drop, NULL);
    TEST_ASSERT_EQUAL_INT(1, dropped_n);
    TEST_ASSERT_EQUAL_UINT32(1, dropped[0]);
    TEST_ASSERT_EQUAL_size_t(100, q.bytes);
    db_region_t *r = db_regions_take(&q);
    TEST_ASSERT_EQUAL_UINT32(2, r->seq);
    free(r);
}

static void test_full_screen_drops_everything_it_covers(void)
{
    db_regions_put(&q, region(1, 0, (db_rect_t){13, 12, 120, 120}, 10), on_drop, NULL);
    db_regions_put(&q, region(2, 0, (db_rect_t){0, 380, 200, 100}, 10), on_drop, NULL);
    db_regions_put(&q, region(3, 1, (db_rect_t){0, 0, 10, 10}, 10), on_drop, NULL);
    db_regions_put(&q, region(4, 0, (db_rect_t){0, 0, 800, 480}, 10), on_drop, NULL);
    TEST_ASSERT_EQUAL_INT(2, dropped_n);
    db_region_t *a = db_regions_take(&q);
    db_region_t *b = db_regions_take(&q);
    TEST_ASSERT_EQUAL_UINT32(3, a->seq); /* other display stays */
    TEST_ASSERT_EQUAL_UINT32(4, b->seq);
    free(a);
    free(b);
}

static void test_smaller_image_keeps_bigger_pending_one(void)
{
    db_regions_put(&q, region(1, 0, (db_rect_t){0, 0, 800, 480}, 10), on_drop, NULL);
    db_regions_put(&q, region(2, 0, (db_rect_t){0, 380, 200, 100}, 10), on_drop, NULL);
    TEST_ASSERT_EQUAL_INT(0, dropped_n);
    db_region_t *a = db_regions_take(&q);
    TEST_ASSERT_EQUAL_UINT32(1, a->seq);
    free(a);
    db_regions_clear(&q, on_drop, NULL);
    TEST_ASSERT_EQUAL_INT(1, dropped_n);
}

static void test_rejects_when_over_budget(void)
{
    db_regions_put(&q, region(1, 0, (db_rect_t){0, 0, 10, 10}, 600), on_drop, NULL);
    db_region_t *big = region(2, 0, (db_rect_t){20, 0, 10, 10}, 600);
    TEST_ASSERT_EQUAL_INT(DB_ERR_NOMEM, db_regions_put(&q, big, on_drop, NULL));
    free(big);
    /* covering the old one frees its budget first */
    TEST_ASSERT_EQUAL_INT(DB_OK,
                          db_regions_put(&q, region(3, 0, (db_rect_t){0, 0, 10, 10}, 900), on_drop, NULL));
    db_regions_clear(&q, on_drop, NULL);
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_keeps_fifo_order);
    RUN_TEST(test_newer_image_replaces_same_area);
    RUN_TEST(test_full_screen_drops_everything_it_covers);
    RUN_TEST(test_smaller_image_keeps_bigger_pending_one);
    RUN_TEST(test_rejects_when_over_budget);
    return UNITY_END();
}
