#include <stdio.h>
#include <string.h>

#include "db_matter.h"
#include "unity.h"

static db_matter_slot_t slots[DB_MAX_DECKS];

void setUp(void)
{
    memset(slots, 0, sizeof(slots));
}

void tearDown(void)
{
}

static uint16_t node(int slot, const char *serial)
{
    bool moved;
    return db_matter_node_ep(&slots[slot], slot, serial, &moved);
}

static uint16_t input(uint16_t node_ep, db_input_kind_t kind, int index)
{
    db_input_t in = {.kind = kind, .index = (uint8_t)index};
    return db_matter_input_ep(node_ep, &in);
}

static void test_a_deck_keeps_its_endpoints_over_a_replug(void)
{
    uint16_t first = node(1, "AL12");
    TEST_ASSERT_EQUAL(first, node(1, "AL12"));
    bool moved = true;
    db_matter_node_ep(&slots[1], 1, "AL12", &moved);
    TEST_ASSERT_FALSE(moved);
}

static void test_another_deck_in_the_slot_gets_new_endpoints(void)
{
    uint16_t a = node(0, "A");
    uint16_t b = node(0, "B");
    TEST_ASSERT_NOT_EQUAL(a, b);
    TEST_ASSERT_NOT_EQUAL(a, node(0, "A")); /* back again: also new, the controller saw B there */
}

static void test_blocks_never_overlap_and_stay_valid(void)
{
    static bool used[1024];
    for (int round = 0; round < DB_MATTER_GENS; round++) {
        for (int s = 0; s < DB_MAX_DECKS; s++) {
            char serial[8];
            snprintf(serial, sizeof(serial), "%d", round);
            uint16_t ep = node(s, serial);
            TEST_ASSERT_EQUAL(0, ep % 64);
            TEST_ASSERT_GREATER_OR_EQUAL(64, ep);
            TEST_ASSERT_FALSE(used[ep / 64]);
            used[ep / 64] = true;
            TEST_ASSERT_LESS_THAN(0xFFFF, input(ep, DB_INPUT_ENCODER_DOWN, DB_MATTER_DIALS - 1));
        }
    }
}

static void test_keys_and_dials_map_inside_the_block(void)
{
    uint16_t ep = node(2, "XL");
    TEST_ASSERT_EQUAL(ep + 1, input(ep, DB_INPUT_KEY_DOWN, 0));
    TEST_ASSERT_EQUAL(ep + 1, input(ep, DB_INPUT_KEY_UP, 0));
    TEST_ASSERT_EQUAL(ep + 36, input(ep, DB_INPUT_KEY_DOWN, 35));
    TEST_ASSERT_EQUAL(ep + 49, input(ep, DB_INPUT_ENCODER_DOWN, 0));
    TEST_ASSERT_EQUAL(ep + 54, input(ep, DB_INPUT_ENCODER_UP, 5));
    TEST_ASSERT_LESS_THAN(ep + 64, input(ep, DB_INPUT_ENCODER_UP, DB_MATTER_DIALS - 1));
}

static void test_turns_touches_and_extra_keys_go_nowhere(void)
{
    uint16_t ep = node(0, "P");
    TEST_ASSERT_EQUAL(0, input(ep, DB_INPUT_ENCODER_TURN, 0));
    TEST_ASSERT_EQUAL(0, input(ep, DB_INPUT_TOUCH_TAP, 0));
    TEST_ASSERT_EQUAL(0, input(ep, DB_INPUT_TOUCH_SWIPE, 0));
    TEST_ASSERT_EQUAL(0, input(ep, DB_INPUT_KEY_DOWN, DB_MATTER_KEYS));
    TEST_ASSERT_EQUAL(0, input(ep, DB_INPUT_ENCODER_DOWN, DB_MATTER_DIALS));
}

static db_sw_t sw;
static int64_t now;
static db_sw_out_t out[8];
static int n;

static void press(bool down)
{
    n = db_sw_input(&sw, down, now, out);
}

static void wait_us(int64_t us)
{
    now += us;
    n = db_sw_tick(&sw, now, out);
}

static void expect(db_sw_event_t e, int count)
{
    TEST_ASSERT_GREATER_THAN(0, n);
    TEST_ASSERT_EQUAL(e, out[n - 1].event);
    TEST_ASSERT_EQUAL(count, out[n - 1].count);
}

static void sw_reset(void)
{
    sw = (db_sw_t){0};
    now = 1000000;
}

static void test_single_press(void)
{
    sw_reset();
    press(true);
    TEST_ASSERT_EQUAL(1, n);
    expect(DB_SW_INITIAL_PRESS, 0);
    now += 100000;
    press(false);
    TEST_ASSERT_EQUAL(1, n);
    expect(DB_SW_SHORT_RELEASE, 0);
    wait_us(DB_SW_MULTI_US - 1);
    TEST_ASSERT_EQUAL(0, n);
    wait_us(1);
    expect(DB_SW_MULTI_COMPLETE, 1);
    TEST_ASSERT_EQUAL(0, db_sw_due(&sw));
}

static void test_double_press_completes_at_once(void)
{
    sw_reset();
    press(true);
    now += 50000;
    press(false);
    now += 100000;
    press(true);
    TEST_ASSERT_EQUAL(2, n);
    TEST_ASSERT_EQUAL(DB_SW_INITIAL_PRESS, out[0].event);
    expect(DB_SW_MULTI_ONGOING, 2);
    now += 50000;
    press(false);
    TEST_ASSERT_EQUAL(2, n);
    TEST_ASSERT_EQUAL(DB_SW_SHORT_RELEASE, out[0].event);
    expect(DB_SW_MULTI_COMPLETE, 2);
    TEST_ASSERT_EQUAL(0, db_sw_due(&sw));
}

static void test_long_press_has_no_multi_press(void)
{
    sw_reset();
    press(true);
    wait_us(DB_SW_LONG_US - 1);
    TEST_ASSERT_EQUAL(0, n);
    wait_us(1);
    expect(DB_SW_LONG_PRESS, 0);
    wait_us(DB_SW_LONG_US);
    TEST_ASSERT_EQUAL(0, n); /* once */
    press(false);
    expect(DB_SW_LONG_RELEASE, 0);
    wait_us(DB_SW_MULTI_US * 2);
    TEST_ASSERT_EQUAL(0, n);
}

static void test_a_held_second_press_is_no_long_press(void)
{
    sw_reset();
    press(true);
    press(false);
    now += 100000;
    press(true);
    wait_us(DB_SW_LONG_US * 2);
    TEST_ASSERT_EQUAL(0, n);
    press(false);
    expect(DB_SW_MULTI_COMPLETE, 2);
}

static void test_a_press_after_the_window_starts_over(void)
{
    sw_reset();
    press(true);
    press(false);
    wait_us(DB_SW_MULTI_US);
    expect(DB_SW_MULTI_COMPLETE, 1);
    press(true);
    TEST_ASSERT_EQUAL(1, n);
    expect(DB_SW_INITIAL_PRESS, 0);
}

static void test_a_repeated_state_is_ignored(void)
{
    sw_reset();
    press(false);
    TEST_ASSERT_EQUAL(0, n);
    press(true);
    press(true);
    TEST_ASSERT_EQUAL(0, n);
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_a_deck_keeps_its_endpoints_over_a_replug);
    RUN_TEST(test_another_deck_in_the_slot_gets_new_endpoints);
    RUN_TEST(test_blocks_never_overlap_and_stay_valid);
    RUN_TEST(test_keys_and_dials_map_inside_the_block);
    RUN_TEST(test_turns_touches_and_extra_keys_go_nowhere);
    RUN_TEST(test_single_press);
    RUN_TEST(test_double_press_completes_at_once);
    RUN_TEST(test_long_press_has_no_multi_press);
    RUN_TEST(test_a_held_second_press_is_no_long_press);
    RUN_TEST(test_a_press_after_the_window_starts_over);
    RUN_TEST(test_a_repeated_state_is_ignored);
    return UNITY_END();
}
