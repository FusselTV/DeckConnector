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

static void test_switches_are_named_like_the_web_ui(void)
{
    uint16_t n = node(0, "AL12");
    char out[17];
    TEST_ASSERT_TRUE(db_matter_label(n, input(n, DB_INPUT_KEY_DOWN, 0), 0, "de", out));
    TEST_ASSERT_EQUAL_STRING("Taste 1", out);
    TEST_ASSERT_TRUE(db_matter_label(n, input(n, DB_INPUT_KEY_DOWN, DB_MATTER_KEYS - 1), 0, "en", out));
    TEST_ASSERT_EQUAL_STRING("Key 48", out);
    TEST_ASSERT_TRUE(db_matter_label(n, input(n, DB_INPUT_ENCODER_DOWN, 0), 8, "de", out));
    TEST_ASSERT_EQUAL_STRING("Drehregler 1", out);
    TEST_ASSERT_TRUE(db_matter_label(n, input(n, DB_INPUT_ENCODER_DOWN, DB_MATTER_DIALS - 1), 0, "en", out));
    TEST_ASSERT_EQUAL_STRING("Dial 8", out);
    TEST_ASSERT_FALSE(db_matter_label(n, n, 0, "de", out));
    TEST_ASSERT_FALSE(db_matter_label(n, (uint16_t)(n + 1 + DB_MATTER_INPUTS), 0, "de", out));
}

static void test_the_neos_extra_keys_are_touch_points(void)
{
    uint16_t n = node(0, "NEO1");
    char out[17];
    TEST_ASSERT_TRUE(db_matter_label(n, input(n, DB_INPUT_KEY_DOWN, 7), 8, "de", out));
    TEST_ASSERT_EQUAL_STRING("Taste 8", out);
    TEST_ASSERT_TRUE(db_matter_label(n, input(n, DB_INPUT_KEY_DOWN, 8), 8, "de", out));
    TEST_ASSERT_EQUAL_STRING("Touch-Punkt 1", out);
    TEST_ASSERT_TRUE(db_matter_label(n, input(n, DB_INPUT_KEY_DOWN, 9), 8, "en", out));
    TEST_ASSERT_EQUAL_STRING("Touch point 2", out);
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
static db_sw_event_t e;

static void press(bool down)
{
    e = db_sw_input(&sw, down, now);
}

static void wait_us(int64_t us)
{
    now += us;
    e = db_sw_tick(&sw, now);
}

static void sw_reset(void)
{
    sw = (db_sw_t){0};
    now = 1000000;
}

static void test_a_short_press_is_told_at_once(void)
{
    sw_reset();
    press(true);
    TEST_ASSERT_EQUAL(DB_SW_INITIAL_PRESS, e);
    now += 100000;
    press(false);
    TEST_ASSERT_EQUAL(DB_SW_SHORT_RELEASE, e);
    TEST_ASSERT_EQUAL(0, db_sw_due(&sw)); /* nothing waits for a second press */
    press(true);
    TEST_ASSERT_EQUAL(DB_SW_INITIAL_PRESS, e); /* a quick second press is a press of its own */
}

static void test_long_press_once_then_long_release(void)
{
    sw_reset();
    press(true);
    wait_us(DB_SW_LONG_US - 1);
    TEST_ASSERT_EQUAL(DB_SW_NONE, e);
    wait_us(1);
    TEST_ASSERT_EQUAL(DB_SW_LONG_PRESS, e);
    wait_us(DB_SW_LONG_US);
    TEST_ASSERT_EQUAL(DB_SW_NONE, e);
    press(false);
    TEST_ASSERT_EQUAL(DB_SW_LONG_RELEASE, e);
    TEST_ASSERT_EQUAL(0, db_sw_due(&sw));
}

static void test_a_repeated_state_is_ignored(void)
{
    sw_reset();
    press(false);
    TEST_ASSERT_EQUAL(DB_SW_NONE, e);
    press(true);
    press(true);
    TEST_ASSERT_EQUAL(DB_SW_NONE, e);
}

static db_sw_deck_t deck;
static const int chord[2] = {DB_MATTER_KEYS, DB_MATTER_KEYS + 3}; /* dials 1 and 4 */
static const int no_chord[2] = {-1, -1};

static db_sw_event_t deck_in(int input, bool down, bool covered)
{
    return e = db_sw_deck_input(&deck, input, down, covered, chord, now);
}

static void test_a_press_on_the_bridges_own_page_goes_nowhere(void)
{
    deck = (db_sw_deck_t){0};
    now = 1000000;
    TEST_ASSERT_EQUAL(DB_SW_NONE, deck_in(4, true, true));
    TEST_ASSERT_EQUAL(DB_SW_NONE, deck_in(4, false, false)); /* the menu closed meanwhile: nothing to end */
    TEST_ASSERT_EQUAL(0, db_sw_due(&deck.sw[4]));
}

static void test_a_release_counts_while_covered(void)
{
    deck = (db_sw_deck_t){0};
    now = 1000000;
    TEST_ASSERT_EQUAL(DB_SW_INITIAL_PRESS, deck_in(4, true, false));
    TEST_ASSERT_EQUAL(DB_SW_SHORT_RELEASE, deck_in(4, false, true)); /* the release that opened the menu */
}

static void test_the_menu_chord_mutes_both_controls(void)
{
    deck = (db_sw_deck_t){0};
    now = 1000000;
    TEST_ASSERT_EQUAL(DB_SW_INITIAL_PRESS, deck_in(chord[0], true, false));
    now += 100000;
    TEST_ASSERT_EQUAL(DB_SW_NONE, deck_in(chord[1], true, false));
    TEST_ASSERT_EQUAL(DB_SW_SHORT_RELEASE, db_sw_deck_tick(&deck, chord[0], now)); /* its press went out */
    for (int i = 0; i < 2; i++) {
        now += DB_SW_LONG_US;
        TEST_ASSERT_EQUAL(DB_SW_NONE, db_sw_deck_tick(&deck, chord[i], now));
        TEST_ASSERT_EQUAL(0, db_sw_due(&deck.sw[chord[i]]));
    }
    TEST_ASSERT_EQUAL(DB_SW_NONE, deck_in(chord[1], false, true));
    TEST_ASSERT_EQUAL(DB_SW_NONE, deck_in(chord[0], false, true));
    TEST_ASSERT_EQUAL(DB_SW_INITIAL_PRESS, deck_in(chord[0], true, false)); /* let go: it works again */
}

static void test_a_long_press_cut_by_the_chord_ends_long(void)
{
    deck = (db_sw_deck_t){0};
    now = 1000000;
    deck_in(chord[0], true, false);
    now += DB_SW_LONG_US;
    TEST_ASSERT_EQUAL(DB_SW_LONG_PRESS, db_sw_deck_tick(&deck, chord[0], now));
    TEST_ASSERT_EQUAL(DB_SW_NONE, deck_in(chord[1], true, false));
    TEST_ASSERT_EQUAL(DB_SW_LONG_RELEASE, deck_in(chord[0], false, false)); /* let go before the next tick */
    TEST_ASSERT_EQUAL(DB_SW_NONE, db_sw_deck_tick(&deck, chord[0], now));
    TEST_ASSERT_EQUAL(DB_SW_NONE, deck_in(chord[0], true, false)); /* again while the other is held: muted */
    TEST_ASSERT_EQUAL(DB_SW_NONE, db_sw_deck_tick(&deck, chord[1], now));
}

static void test_an_unplug_releases_what_a_controller_saw_held(void)
{
    deck = (db_sw_deck_t){0};
    now = 1000000;
    deck_in(2, true, false);
    deck_in(5, true, false);
    now += DB_SW_LONG_US;
    db_sw_deck_tick(&deck, 5, now);
    deck_in(chord[0], true, false);
    deck_in(chord[1], true, false);
    db_sw_deck_tick(&deck, chord[0], now);
    db_sw_event_t gone[DB_MATTER_INPUTS];
    db_sw_deck_gone(&deck, now, gone);
    for (int i = 0; i < DB_MATTER_INPUTS; i++) {
        db_sw_event_t want = i == 2 ? DB_SW_SHORT_RELEASE : i == 5 ? DB_SW_LONG_RELEASE : DB_SW_NONE;
        TEST_ASSERT_EQUAL(want, gone[i]);
        TEST_ASSERT_FALSE(deck.sw[i].down);
    }
    TEST_ASSERT_EQUAL(DB_SW_INITIAL_PRESS, deck_in(chord[1], true, false)); /* plugged in again: all let go */
}

static void test_two_other_controls_together_both_count(void)
{
    deck = (db_sw_deck_t){0};
    now = 1000000;
    deck_in(0, true, false);
    TEST_ASSERT_EQUAL(DB_SW_INITIAL_PRESS, deck_in(1, true, false));
    TEST_ASSERT_EQUAL(DB_SW_INITIAL_PRESS, db_sw_deck_input(&deck, chord[1], true, false, no_chord, now));
}

static void test_only_the_deck_and_the_debug_route_reach_matter(void)
{
    TEST_ASSERT_TRUE(db_matter_takes(DB_MATTER_FROM_DECK));
    TEST_ASSERT_TRUE(db_matter_takes(DB_MATTER_FROM_DEBUG));
    TEST_ASSERT_FALSE(db_matter_takes(DB_MATTER_FROM_API)); /* /api/input and /api/ws, "real" or not */
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_a_deck_keeps_its_endpoints_over_a_replug);
    RUN_TEST(test_another_deck_in_the_slot_gets_new_endpoints);
    RUN_TEST(test_blocks_never_overlap_and_stay_valid);
    RUN_TEST(test_keys_and_dials_map_inside_the_block);
    RUN_TEST(test_switches_are_named_like_the_web_ui);
    RUN_TEST(test_the_neos_extra_keys_are_touch_points);
    RUN_TEST(test_turns_touches_and_extra_keys_go_nowhere);
    RUN_TEST(test_a_short_press_is_told_at_once);
    RUN_TEST(test_long_press_once_then_long_release);
    RUN_TEST(test_a_repeated_state_is_ignored);
    RUN_TEST(test_a_press_on_the_bridges_own_page_goes_nowhere);
    RUN_TEST(test_a_release_counts_while_covered);
    RUN_TEST(test_the_menu_chord_mutes_both_controls);
    RUN_TEST(test_a_long_press_cut_by_the_chord_ends_long);
    RUN_TEST(test_an_unplug_releases_what_a_controller_saw_held);
    RUN_TEST(test_two_other_controls_together_both_count);
    RUN_TEST(test_only_the_deck_and_the_debug_route_reach_matter);
    return UNITY_END();
}
