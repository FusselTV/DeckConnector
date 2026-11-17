#include "db_device.h"
#include "unity.h"

#define S   1000000LL
#define MIN (60 * S)

static db_stuck_state_t st;
static bool restarted;

void setUp(void)
{
    st = (db_stuck_state_t){0};
    restarted = true; /* used up: these tests are about the cycles */
}

void tearDown(void)
{
}

/* The deck hangs again as soon as it is allowed to, n times; returns when the last cycle came. */
static int64_t hang_on(int64_t now, int n, bool others)
{
    for (int i = 0; i < n; now += S) {
        if (db_stuck_next(&st, now, true, others, &restarted) == DB_STUCK_CYCLE) {
            i++;
        }
    }
    return now - S;
}

static void test_first_hang_cycles_at_once(void)
{
    TEST_ASSERT_EQUAL(DB_STUCK_CYCLE, db_stuck_next(&st, 20 * MIN, true, true, &restarted));
    TEST_ASSERT_EQUAL_INT64(0, st.backoff);
}

static void test_alone_the_deck_is_tried_again_every_5_min_at_most(void)
{
    hang_on(20 * MIN, 2, false);
    TEST_ASSERT_EQUAL_INT64(10 * S, st.backoff);
    hang_on(st.last, 10, false);
    TEST_ASSERT_EQUAL_INT64(5 * MIN, st.backoff);
}

static void test_with_others_a_hung_deck_is_left_alone_longer(void)
{
    int64_t first = hang_on(20 * MIN, 2, true);
    TEST_ASSERT_EQUAL_INT64(10 * MIN, st.backoff);
    TEST_ASSERT_EQUAL(DB_STUCK_WAIT, db_stuck_next(&st, first + 9 * MIN, true, true, &restarted));
    hang_on(st.last, 10, true);
    TEST_ASSERT_EQUAL_INT64(60 * MIN, st.backoff);
}

static void test_the_floor_holds_a_cycle_back(void)
{
    TEST_ASSERT_EQUAL(DB_STUCK_WAIT, db_stuck_next(&st, 20 * MIN, false, false, &restarted));
    TEST_ASSERT_EQUAL_INT64(0, st.last);
}

static void test_a_deck_that_stays_well_starts_over(void)
{
    hang_on(20 * MIN, 3, true);
    TEST_ASSERT_EQUAL(DB_STUCK_CYCLE, db_stuck_next(&st, st.last + 3 * 60 * MIN, true, true, &restarted));
    TEST_ASSERT_EQUAL_INT64(0, st.backoff);
}

static void test_one_restart_after_10_min_uptime(void)
{
    restarted = false;
    hang_on(MIN, 2, false); /* too early */
    TEST_ASSERT_FALSE(restarted);
    st = (db_stuck_state_t){0};
    db_stuck_next(&st, 20 * MIN, true, true, &restarted);
    TEST_ASSERT_EQUAL(DB_STUCK_RESTART, db_stuck_next(&st, 20 * MIN + 5 * S, true, true, &restarted));
    TEST_ASSERT_TRUE(restarted);
    TEST_ASSERT_EQUAL(DB_STUCK_CYCLE, db_stuck_next(&st, 20 * MIN + 10 * S, true, true, &restarted));
}

static void test_an_hour_without_a_hang_arms_the_restart_again(void)
{
    db_stuck_next(&st, 20 * MIN, true, false, &restarted);
    db_stuck_next(&st, 20 * MIN + 61 * MIN, true, false, &restarted);
    TEST_ASSERT_FALSE(restarted);
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_first_hang_cycles_at_once);
    RUN_TEST(test_alone_the_deck_is_tried_again_every_5_min_at_most);
    RUN_TEST(test_with_others_a_hung_deck_is_left_alone_longer);
    RUN_TEST(test_the_floor_holds_a_cycle_back);
    RUN_TEST(test_a_deck_that_stays_well_starts_over);
    RUN_TEST(test_one_restart_after_10_min_uptime);
    RUN_TEST(test_an_hour_without_a_hang_arms_the_restart_again);
    return UNITY_END();
}
