#include "db_wake.h"
#include "unity.h"

static db_wake_t w;
static int64_t now;

void setUp(void)
{
    now = 1000000;
    db_wake_init(&w, now);
}

void tearDown(void)
{
}

/* True when the event reaches the hosts. */
static bool in(int deck, db_input_kind_t kind, int index)
{
    bool woke;
    db_input_t ev = {.kind = kind, .index = (uint8_t)index};
    return !db_wake_input(&w, deck, &ev, now, &woke);
}

static void go_dark(void)
{
    TEST_ASSERT_TRUE(db_wake_set(&w, true, now));
}

static void test_input_goes_through_while_lit(void)
{
    TEST_ASSERT_TRUE(in(0, DB_INPUT_KEY_DOWN, 3));
    TEST_ASSERT_TRUE(in(0, DB_INPUT_KEY_UP, 3));
    TEST_ASSERT_TRUE(in(0, DB_INPUT_ENCODER_TURN, 1));
    TEST_ASSERT_TRUE(in(0, DB_INPUT_TOUCH_TAP, 0));
}

static void test_the_wake_press_and_its_release_are_swallowed(void)
{
    go_dark();
    TEST_ASSERT_FALSE(in(0, DB_INPUT_KEY_DOWN, 3));
    TEST_ASSERT_FALSE(w.dark);
    TEST_ASSERT_FALSE(in(0, DB_INPUT_KEY_UP, 3));
    TEST_ASSERT_TRUE(in(0, DB_INPUT_KEY_DOWN, 3));
    TEST_ASSERT_TRUE(in(0, DB_INPUT_KEY_UP, 3));
}

static void test_everything_on_that_deck_waits_for_the_release(void)
{
    go_dark();
    in(0, DB_INPUT_ENCODER_DOWN, 0);
    TEST_ASSERT_FALSE(in(0, DB_INPUT_ENCODER_DOWN, 3)); /* the menu chord's second half */
    TEST_ASSERT_FALSE(in(0, DB_INPUT_ENCODER_UP, 0));
    TEST_ASSERT_FALSE(in(0, DB_INPUT_KEY_DOWN, 1));
    TEST_ASSERT_FALSE(in(0, DB_INPUT_ENCODER_UP, 3));
    TEST_ASSERT_FALSE(in(0, DB_INPUT_KEY_UP, 1));
    TEST_ASSERT_TRUE(in(0, DB_INPUT_KEY_DOWN, 1));
}

static void test_another_deck_works_while_the_waking_one_is_held(void)
{
    go_dark();
    in(1, DB_INPUT_KEY_DOWN, 0);
    TEST_ASSERT_TRUE(in(0, DB_INPUT_KEY_DOWN, 0));
    TEST_ASSERT_TRUE(in(0, DB_INPUT_KEY_UP, 0));
    TEST_ASSERT_FALSE(in(1, DB_INPUT_KEY_UP, 0));
}

static void test_a_tap_or_swipe_alone_wakes(void)
{
    go_dark();
    TEST_ASSERT_FALSE(in(0, DB_INPUT_TOUCH_SWIPE, 0));
    TEST_ASSERT_TRUE(in(0, DB_INPUT_TOUCH_TAP, 0));
}

static void test_the_waking_turn_goes_on_until_it_pauses(void)
{
    go_dark();
    TEST_ASSERT_FALSE(in(0, DB_INPUT_ENCODER_TURN, 2));
    now += DB_WAKE_TURN_GAP_US - 1;
    TEST_ASSERT_FALSE(in(0, DB_INPUT_ENCODER_TURN, 2));
    TEST_ASSERT_TRUE(in(0, DB_INPUT_ENCODER_TURN, 1));
    now += DB_WAKE_TURN_GAP_US;
    TEST_ASSERT_TRUE(in(0, DB_INPUT_ENCODER_TURN, 2));
}

static void test_a_release_held_from_before_goes_through(void)
{
    in(0, DB_INPUT_KEY_DOWN, 5);
    go_dark();
    TEST_ASSERT_TRUE(in(0, DB_INPUT_KEY_UP, 5));
    TEST_ASSERT_FALSE(w.dark);
}

static void test_dark_after_the_idle_time(void)
{
    bool dark;
    TEST_ASSERT_FALSE(db_wake_due(&w, 10, false, now + 9999999, &dark));
    TEST_ASSERT_TRUE(db_wake_due(&w, 10, false, now + 10000000, &dark));
    TEST_ASSERT_TRUE(dark);
    TEST_ASSERT_FALSE(db_wake_due(&w, 0, false, now + 86400000000LL, &dark));
}

static void test_a_held_control_keeps_it_lit(void)
{
    bool dark;
    in(2, DB_INPUT_ENCODER_DOWN, 0);
    TEST_ASSERT_FALSE(db_wake_due(&w, 10, false, now + 60000000, &dark));
    in(2, DB_INPUT_ENCODER_UP, 0);
    TEST_ASSERT_TRUE(db_wake_due(&w, 10, false, now + 10000000, &dark));
}

static void test_a_question_on_the_deck_wakes_it(void)
{
    bool dark;
    go_dark();
    TEST_ASSERT_FALSE(db_wake_due(&w, 10, false, now, &dark));
    TEST_ASSERT_TRUE(db_wake_due(&w, 10, true, now, &dark));
    TEST_ASSERT_FALSE(dark);
    TEST_ASSERT_TRUE(db_wake_set(&w, false, now));
    TEST_ASSERT_TRUE(in(0, DB_INPUT_KEY_DOWN, 0)); /* it answers the question */
}

static void test_waking_starts_the_idle_time_again(void)
{
    go_dark();
    now += 3600000000LL;
    db_wake_set(&w, false, now);
    bool dark;
    TEST_ASSERT_FALSE(db_wake_due(&w, 10, false, now + 1000000, &dark));
    now += 3600000000LL;
    TEST_ASSERT_FALSE(db_wake_set(&w, false, now)); /* lit already: only the idle time starts again */
    TEST_ASSERT_FALSE(db_wake_due(&w, 10, false, now + 1000000, &dark));
}

static void test_an_unplugged_deck_leaves_nothing_held(void)
{
    bool dark;
    go_dark();
    TEST_ASSERT_FALSE(in(0, DB_INPUT_KEY_DOWN, 3)); /* the wake press, unplugged before its release */
    db_wake_forget(&w, 0);
    TEST_ASSERT_TRUE(in(0, DB_INPUT_KEY_DOWN, 5));
    TEST_ASSERT_TRUE(in(0, DB_INPUT_KEY_UP, 5));
    TEST_ASSERT_TRUE(in(1, DB_INPUT_KEY_DOWN, 0)); /* held on another deck when it went */
    db_wake_forget(&w, 1);
    TEST_ASSERT_TRUE(db_wake_due(&w, 10, false, now + 10000000, &dark));
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_input_goes_through_while_lit);
    RUN_TEST(test_the_wake_press_and_its_release_are_swallowed);
    RUN_TEST(test_everything_on_that_deck_waits_for_the_release);
    RUN_TEST(test_another_deck_works_while_the_waking_one_is_held);
    RUN_TEST(test_a_tap_or_swipe_alone_wakes);
    RUN_TEST(test_the_waking_turn_goes_on_until_it_pauses);
    RUN_TEST(test_a_release_held_from_before_goes_through);
    RUN_TEST(test_dark_after_the_idle_time);
    RUN_TEST(test_a_held_control_keeps_it_lit);
    RUN_TEST(test_a_question_on_the_deck_wakes_it);
    RUN_TEST(test_waking_starts_the_idle_time_again);
    RUN_TEST(test_an_unplugged_deck_leaves_nothing_held);
    return UNITY_END();
}
