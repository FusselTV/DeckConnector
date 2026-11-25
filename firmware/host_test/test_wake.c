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
    return !db_wake_input(&w, deck, false, false, &ev, now, &woke);
}

/* The same on a deck drawn by a host that owns the screen. */
static bool in_owned(int deck, db_input_kind_t kind, int index)
{
    bool woke;
    db_input_t ev = {.kind = kind, .index = (uint8_t)index};
    return !db_wake_input(&w, deck, false, true, &ev, now, &woke);
}

/* The same on a deck whose own brightness on the box is 0. */
static bool in_black(int deck, db_input_kind_t kind, int index, bool *woke)
{
    db_input_t ev = {.kind = kind, .index = (uint8_t)index};
    return !db_wake_input(&w, deck, true, false, &ev, now, woke);
}

static void go_dark(void)
{
    TEST_ASSERT_TRUE(db_wake_set(&w, 0, now));
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
    TEST_ASSERT_EQUAL(-1, w.cap);
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

static void test_the_last_keys_of_the_plus_xl_count_too(void)
{
    go_dark();
    TEST_ASSERT_FALSE(in(0, DB_INPUT_KEY_DOWN, 35));
    TEST_ASSERT_FALSE(in(0, DB_INPUT_KEY_DOWN, 4));
    TEST_ASSERT_EQUAL_INT64(0, db_wake_idle_us(&w, now + 60000000));
    TEST_ASSERT_FALSE(in(0, DB_INPUT_KEY_UP, 35));
    TEST_ASSERT_FALSE(in(0, DB_INPUT_KEY_UP, 4));
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
    TEST_ASSERT_EQUAL(-1, w.cap);
}

static void test_dark_after_the_idle_time(void)
{
    int cap;
    TEST_ASSERT_FALSE(db_wake_due(&w, 10, 0, false, now + 9999999, &cap));
    TEST_ASSERT_TRUE(db_wake_due(&w, 10, 0, false, now + 10000000, &cap));
    TEST_ASSERT_EQUAL(0, cap);
    TEST_ASSERT_FALSE(db_wake_due(&w, 0, 0, false, now + 86400000000LL, &cap));
}

static void test_a_held_control_keeps_it_lit(void)
{
    int cap;
    in(2, DB_INPUT_ENCODER_DOWN, 0);
    TEST_ASSERT_FALSE(db_wake_due(&w, 10, 0, false, now + 60000000, &cap));
    in(2, DB_INPUT_ENCODER_UP, 0);
    TEST_ASSERT_TRUE(db_wake_due(&w, 10, 0, false, now + 10000000, &cap));
}

static void test_a_question_on_the_deck_wakes_it(void)
{
    int cap;
    go_dark();
    TEST_ASSERT_FALSE(db_wake_due(&w, 10, 0, false, now, &cap));
    TEST_ASSERT_TRUE(db_wake_due(&w, 10, 0, true, now, &cap));
    TEST_ASSERT_EQUAL(-1, cap);
    TEST_ASSERT_TRUE(db_wake_set(&w, -1, now));
    TEST_ASSERT_TRUE(in(0, DB_INPUT_KEY_DOWN, 0)); /* it answers the question */
}

static void test_waking_starts_the_idle_time_again(void)
{
    go_dark();
    now += 3600000000LL;
    db_wake_set(&w, -1, now);
    int cap;
    TEST_ASSERT_FALSE(db_wake_due(&w, 10, 0, false, now + 1000000, &cap));
    now += 3600000000LL;
    TEST_ASSERT_FALSE(db_wake_set(&w, -1, now)); /* lit already: only the idle time starts again */
    TEST_ASSERT_FALSE(db_wake_due(&w, 10, 0, false, now + 1000000, &cap));
}

static void test_an_unplugged_deck_leaves_nothing_held(void)
{
    int cap;
    go_dark();
    TEST_ASSERT_FALSE(in(0, DB_INPUT_KEY_DOWN, 3)); /* the wake press, unplugged before its release */
    db_wake_forget(&w, 0);
    TEST_ASSERT_TRUE(in(0, DB_INPUT_KEY_DOWN, 5));
    TEST_ASSERT_TRUE(in(0, DB_INPUT_KEY_UP, 5));
    TEST_ASSERT_TRUE(in(1, DB_INPUT_KEY_DOWN, 0)); /* held on another deck when it went */
    db_wake_forget(&w, 1);
    TEST_ASSERT_TRUE(db_wake_due(&w, 10, 0, false, now + 10000000, &cap));
}

static void test_dimmed_after_the_idle_time_and_a_press_works_and_lights(void)
{
    int cap;
    TEST_ASSERT_TRUE(db_wake_due(&w, 10, 30, false, now + 10000000, &cap));
    TEST_ASSERT_EQUAL(30, cap);
    db_wake_set(&w, cap, now);
    TEST_ASSERT_FALSE(db_wake_due(&w, 10, 30, false, now + 20000000, &cap)); /* stays dimmed */
    bool woke;
    db_input_t ev = {.kind = DB_INPUT_KEY_DOWN, .index = 4};
    /* it can be read: the press works */
    TEST_ASSERT_FALSE(db_wake_input(&w, 0, false, false, &ev, now, &woke));
    TEST_ASSERT_TRUE(woke);
    TEST_ASSERT_EQUAL(-1, w.cap);
    TEST_ASSERT_TRUE(in(0, DB_INPUT_KEY_UP, 4));
}

static void test_a_deck_at_brightness_0_swallows_its_waking_gesture(void)
{
    bool woke;
    TEST_ASSERT_FALSE(in_black(1, DB_INPUT_KEY_DOWN, 2, &woke));
    TEST_ASSERT_FALSE(woke); /* the screen saver had nothing to do with it */
    TEST_ASSERT_FALSE(in(1, DB_INPUT_KEY_UP, 2));
    TEST_ASSERT_TRUE(in(0, DB_INPUT_KEY_DOWN, 2)); /* another deck is not touched */
    TEST_ASSERT_TRUE(in(1, DB_INPUT_KEY_DOWN, 2)); /* lit by now */
}

static void test_the_switch_turned_off_wins_over_a_dim(void)
{
    int cap;
    db_wake_set(&w, 0, now);
    TEST_ASSERT_FALSE(db_wake_due(&w, 10, 30, false, now + 60000000, &cap));
    TEST_ASSERT_FALSE(in(0, DB_INPUT_KEY_DOWN, 0));
    TEST_ASSERT_EQUAL(-1, w.cap);
}

static void test_a_host_that_owns_the_screen_gets_the_waking_gesture(void)
{
    go_dark();
    TEST_ASSERT_TRUE(in_owned(0, DB_INPUT_KEY_DOWN, 3));
    TEST_ASSERT_TRUE(in_owned(0, DB_INPUT_ENCODER_TURN, 1));
    TEST_ASSERT_TRUE(in_owned(0, DB_INPUT_KEY_UP, 3));
    TEST_ASSERT_EQUAL(-1, w.cap); /* the other decks wake with it */
}

static void test_a_gesture_hidden_before_the_host_owned_the_screen_hides_nothing_after(void)
{
    go_dark();
    TEST_ASSERT_FALSE(in(0, DB_INPUT_KEY_DOWN, 3));
    TEST_ASSERT_TRUE(in_owned(0, DB_INPUT_KEY_DOWN, 4));
    TEST_ASSERT_TRUE(in_owned(0, DB_INPUT_KEY_UP, 3));
    TEST_ASSERT_TRUE(in_owned(0, DB_INPUT_KEY_UP, 4));
}

static void test_the_box_swallows_again_once_the_host_is_gone(void)
{
    TEST_ASSERT_TRUE(in_owned(0, DB_INPUT_KEY_DOWN, 3));
    TEST_ASSERT_TRUE(in_owned(0, DB_INPUT_KEY_UP, 3));
    go_dark();
    TEST_ASSERT_FALSE(in(0, DB_INPUT_KEY_DOWN, 3));
    TEST_ASSERT_FALSE(in(0, DB_INPUT_KEY_UP, 3));
}

static void test_shown_brightness(void)
{
    TEST_ASSERT_EQUAL(80, db_wake_shown(-1, 80));
    TEST_ASSERT_EQUAL(30, db_wake_shown(30, 80));
    TEST_ASSERT_EQUAL(10, db_wake_shown(30, 10)); /* a host's lower level stays */
    TEST_ASSERT_EQUAL(0, db_wake_shown(0, 80));
    TEST_ASSERT_EQUAL(30, db_wake_shown(30, -1));
    TEST_ASSERT_EQUAL(-1, db_wake_shown(-1, -1));
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_input_goes_through_while_lit);
    RUN_TEST(test_the_wake_press_and_its_release_are_swallowed);
    RUN_TEST(test_everything_on_that_deck_waits_for_the_release);
    RUN_TEST(test_another_deck_works_while_the_waking_one_is_held);
    RUN_TEST(test_the_last_keys_of_the_plus_xl_count_too);
    RUN_TEST(test_a_tap_or_swipe_alone_wakes);
    RUN_TEST(test_the_waking_turn_goes_on_until_it_pauses);
    RUN_TEST(test_a_release_held_from_before_goes_through);
    RUN_TEST(test_dark_after_the_idle_time);
    RUN_TEST(test_a_held_control_keeps_it_lit);
    RUN_TEST(test_a_question_on_the_deck_wakes_it);
    RUN_TEST(test_waking_starts_the_idle_time_again);
    RUN_TEST(test_an_unplugged_deck_leaves_nothing_held);
    RUN_TEST(test_dimmed_after_the_idle_time_and_a_press_works_and_lights);
    RUN_TEST(test_a_deck_at_brightness_0_swallows_its_waking_gesture);
    RUN_TEST(test_the_switch_turned_off_wins_over_a_dim);
    RUN_TEST(test_a_host_that_owns_the_screen_gets_the_waking_gesture);
    RUN_TEST(test_a_gesture_hidden_before_the_host_owned_the_screen_hides_nothing_after);
    RUN_TEST(test_the_box_swallows_again_once_the_host_is_gone);
    RUN_TEST(test_shown_brightness);
    return UNITY_END();
}
