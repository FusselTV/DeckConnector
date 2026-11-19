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

static const db_matter_layout_t plus = {.keys = 8, .dials = 4, .zones = 4, .strip_w = 800};
static const db_matter_layout_t plus_xl = {.keys = 36, .dials = 6, .zones = 6, .strip_w = 1200};
static const db_matter_layout_t neo = {.keys = 10, .image_keys = 8};
static const db_matter_layout_t biggest = {
    .keys = DB_MATTER_KEYS, .dials = DB_MATTER_DIALS, .zones = 8, .strip_w = 800};

static db_press_t how;

static int input(const db_matter_layout_t *l, db_input_kind_t kind, int index)
{
    db_input_t in = {.kind = kind, .index = (uint8_t)index};
    return db_matter_input(l, &in, &how);
}

static int turn(const db_matter_layout_t *l, int dial, int ticks)
{
    db_input_t in = {.kind = DB_INPUT_ENCODER_TURN, .index = (uint8_t)dial, .value = (int16_t)ticks};
    return db_matter_input(l, &in, &how);
}

static int touch(const db_matter_layout_t *l, db_input_kind_t kind, int x, int x2)
{
    db_input_t in = {.kind = kind, .x = (uint16_t)x, .y = 420, .x2 = (uint16_t)x2, .y2 = 420};
    return db_matter_input(l, &in, &how);
}

static const char *label(const db_matter_layout_t *l, int i, const char *lang)
{
    static char out[17];
    return db_matter_label(l, i, lang, out) ? out : NULL;
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
            TEST_ASSERT_LESS_THAN(0xFFFF, ep + DB_MATTER_INPUTS);
        }
    }
}

static void test_keys_and_dials_keep_their_switches(void)
{
    TEST_ASSERT_EQUAL(0, input(&plus, DB_INPUT_KEY_DOWN, 0));
    TEST_ASSERT_EQUAL(DB_PRESS_DOWN, how);
    TEST_ASSERT_EQUAL(7, input(&plus, DB_INPUT_KEY_UP, 7));
    TEST_ASSERT_EQUAL(DB_PRESS_UP, how);
    TEST_ASSERT_EQUAL(48, input(&plus, DB_INPUT_ENCODER_DOWN, 0)); /* endpoint 49 of the block, as before */
    TEST_ASSERT_EQUAL(51, input(&plus, DB_INPUT_ENCODER_UP, 3));
    TEST_ASSERT_EQUAL(35, input(&plus_xl, DB_INPUT_KEY_DOWN, 35));
    TEST_ASSERT_EQUAL(53, input(&plus_xl, DB_INPUT_ENCODER_UP, 5));
    TEST_ASSERT_EQUAL(-1, input(&plus, DB_INPUT_KEY_DOWN, 8));
    TEST_ASSERT_EQUAL(-1, input(&plus, DB_INPUT_ENCODER_DOWN, 4));
}

static void test_turns_and_touches_take_the_free_switches_in_order(void)
{
    TEST_ASSERT_EQUAL(8, turn(&plus, 0, -1));
    TEST_ASSERT_EQUAL(DB_PRESS_SHORT, how);
    TEST_ASSERT_EQUAL(9, turn(&plus, 0, 3));
    TEST_ASSERT_EQUAL(15, turn(&plus, 3, 1));
    TEST_ASSERT_EQUAL(-1, turn(&plus, 0, 0));
    TEST_ASSERT_EQUAL(-1, turn(&plus, 4, 1));
    TEST_ASSERT_EQUAL(16, touch(&plus, DB_INPUT_TOUCH_TAP, 0, 0));
    TEST_ASSERT_EQUAL(DB_PRESS_SHORT, how);
    TEST_ASSERT_EQUAL(17, touch(&plus, DB_INPUT_TOUCH_TAP, 200, 0)); /* zone 2 starts at 200 */
    TEST_ASSERT_EQUAL(19, touch(&plus, DB_INPUT_TOUCH_LONG, 799, 0));
    TEST_ASSERT_EQUAL(DB_PRESS_LONG, how);
    TEST_ASSERT_EQUAL(19, touch(&plus, DB_INPUT_TOUCH_TAP, 900, 0));
    TEST_ASSERT_EQUAL(20, touch(&plus, DB_INPUT_TOUCH_SWIPE, 500, 100));
    TEST_ASSERT_EQUAL(21, touch(&plus, DB_INPUT_TOUCH_SWIPE, 100, 500));
    TEST_ASSERT_EQUAL(-1, touch(&plus, DB_INPUT_TOUCH_SWIPE, 100, 100));
    int used = 0;
    for (int i = 0; i < DB_MATTER_INPUTS; i++) {
        used += db_matter_used(&plus, i);
    }
    TEST_ASSERT_EQUAL(8 + 4 + 8 + 4 + 2, used);
}

static void test_the_biggest_touch_deck_fits_its_block(void)
{
    bool seen[DB_MATTER_INPUTS] = {0};
    int all[6 * 2 + 6 + 2];
    int n = 0;
    for (int d = 0; d < 6; d++) {
        all[n++] = turn(&plus_xl, d, -1);
        all[n++] = turn(&plus_xl, d, 1);
    }
    for (int z = 0; z < 6; z++) {
        all[n++] = touch(&plus_xl, DB_INPUT_TOUCH_TAP, z * 200 + 100, 0);
    }
    all[n++] = touch(&plus_xl, DB_INPUT_TOUCH_SWIPE, 600, 0);
    all[n++] = touch(&plus_xl, DB_INPUT_TOUCH_SWIPE, 600, 1100);
    for (int i = 0; i < n; i++) {
        TEST_ASSERT_TRUE(all[i] >= 36 && all[i] < DB_MATTER_INPUTS);
        TEST_ASSERT_FALSE(all[i] >= 48 && all[i] < 54); /* the dials' own */
        TEST_ASSERT_FALSE(seen[all[i]]);
        seen[all[i]] = true;
    }
    TEST_ASSERT_EQUAL(61, all[n - 1]);
}

static void test_what_does_not_fit_goes_nowhere(void)
{
    TEST_ASSERT_EQUAL(56, turn(&biggest, 0, -1)); /* 7 free switches: the first turns */
    TEST_ASSERT_EQUAL(62, turn(&biggest, 3, -1));
    TEST_ASSERT_EQUAL(-1, turn(&biggest, 3, 1));
    TEST_ASSERT_EQUAL(-1, touch(&biggest, DB_INPUT_TOUCH_TAP, 0, 0));
    TEST_ASSERT_FALSE(db_matter_used(&biggest, DB_MATTER_INPUTS));
    TEST_ASSERT_FALSE(db_matter_used(&biggest, -1));
}

static void test_a_deck_without_strip_or_dials_has_no_extras(void)
{
    TEST_ASSERT_EQUAL(-1, touch(&neo, DB_INPUT_TOUCH_TAP, 10, 0));
    TEST_ASSERT_EQUAL(-1, touch(&neo, DB_INPUT_TOUCH_SWIPE, 10, 100));
    TEST_ASSERT_EQUAL(-1, turn(&neo, 0, 1));
    TEST_ASSERT_FALSE(db_matter_used(&neo, 10));
    db_matter_layout_t knobs = {.keys = 14, .dials = 5}; /* Stream Dock N4: dials, no strip */
    TEST_ASSERT_EQUAL(14, turn(&knobs, 0, -1));
    TEST_ASSERT_EQUAL(-1, touch(&knobs, DB_INPUT_TOUCH_TAP, 10, 0));
}

static void test_switches_are_named_like_the_web_ui(void)
{
    TEST_ASSERT_EQUAL_STRING("Taste 1", label(&plus, 0, "de"));
    TEST_ASSERT_EQUAL_STRING("Key 8", label(&plus, 7, "en"));
    TEST_ASSERT_EQUAL_STRING("Drehregler 1", label(&plus, 48, "de"));
    TEST_ASSERT_EQUAL_STRING("Dial 4", label(&plus, 51, "en"));
    TEST_ASSERT_EQUAL_STRING("Regler 1 links", label(&plus, 8, "de"));
    TEST_ASSERT_EQUAL_STRING("Dial 4 right", label(&plus, 15, "en"));
    TEST_ASSERT_EQUAL_STRING("Touchfeld 1", label(&plus, 16, "de"));
    TEST_ASSERT_EQUAL_STRING("Touch zone 4", label(&plus, 19, "en"));
    TEST_ASSERT_EQUAL_STRING("Wischen links", label(&plus, 20, "de"));
    TEST_ASSERT_EQUAL_STRING("Swipe right", label(&plus, 21, "en"));
    TEST_ASSERT_EQUAL_STRING("Key 1", label(&plus, 0, "fr"));
    TEST_ASSERT_EQUAL_STRING("Regler 6 rechts", label(&plus_xl, turn(&plus_xl, 5, 1), "de"));
    TEST_ASSERT_EQUAL_STRING("Wischen rechts", label(&plus_xl, 61, "de"));
    TEST_ASSERT_NULL(label(&plus, 22, "de"));
    TEST_ASSERT_NULL(label(&plus, 52, "de"));
    TEST_ASSERT_NULL(label(&plus, -1, "de"));
    TEST_ASSERT_NULL(label(&plus, DB_MATTER_INPUTS, "de"));
}

static void test_the_neos_extra_keys_are_touch_points(void)
{
    TEST_ASSERT_EQUAL_STRING("Taste 8", label(&neo, 7, "de"));
    TEST_ASSERT_EQUAL_STRING("Touch-Punkt 1", label(&neo, 8, "de"));
    TEST_ASSERT_EQUAL_STRING("Touch point 2", label(&neo, 9, "en"));
}

static void test_switches_count_in_deck_order(void)
{
    const db_matter_layout_t *all[] = {&plus, &plus_xl, &neo, &biggest};
    for (size_t k = 0; k < sizeof(all) / sizeof(all[0]); k++) {
        bool seen[DB_MATTER_INPUTS + 1] = {0};
        int used = 0;
        for (int i = 0; i < DB_MATTER_INPUTS; i++) {
            int n = db_matter_order(all[k], i);
            TEST_ASSERT_EQUAL(db_matter_used(all[k], i), n > 0);
            TEST_ASSERT_FALSE(n && seen[n]);
            seen[n] = true;
            used += n > 0;
        }
        for (int n = 1; n <= used; n++) {
            TEST_ASSERT_TRUE(seen[n]);
        }
    }
    TEST_ASSERT_EQUAL(8, db_matter_order(&plus, 7));      /* Taste 8 */
    TEST_ASSERT_EQUAL(9, db_matter_order(&plus, 48));     /* Drehregler 1 */
    TEST_ASSERT_EQUAL(13, db_matter_order(&plus, 8));     /* Regler 1 links */
    TEST_ASSERT_EQUAL(21, db_matter_order(&plus, 16));    /* Touchfeld 1 */
    TEST_ASSERT_EQUAL(26, db_matter_order(&plus, 21));    /* Wischen rechts */
    TEST_ASSERT_EQUAL(54, db_matter_order(&plus_xl, 47)); /* Regler 6 rechts, before the dials' own ids */
    TEST_ASSERT_EQUAL(55, db_matter_order(&plus_xl, 54)); /* Touchfeld 1, after them */
    TEST_ASSERT_EQUAL(0, db_matter_order(&plus, -1));
    TEST_ASSERT_EQUAL(10, db_matter_order(&neo, 9));
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

static void test_a_tap_or_detent_is_a_whole_short_press(void)
{
    deck = (db_sw_deck_t){0};
    db_sw_event_t out[3];
    TEST_ASSERT_EQUAL(2, db_sw_deck_pulse(&deck, 8, DB_PRESS_SHORT, false, 1000000, out));
    TEST_ASSERT_EQUAL(DB_SW_INITIAL_PRESS, out[0]);
    TEST_ASSERT_EQUAL(DB_SW_SHORT_RELEASE, out[1]);
    TEST_ASSERT_EQUAL(3, db_sw_deck_pulse(&deck, 16, DB_PRESS_LONG, false, 1000000, out));
    TEST_ASSERT_EQUAL(DB_SW_INITIAL_PRESS, out[0]);
    TEST_ASSERT_EQUAL(DB_SW_LONG_PRESS, out[1]);
    TEST_ASSERT_EQUAL(DB_SW_LONG_RELEASE, out[2]);
    TEST_ASSERT_EQUAL(0, db_sw_due(&deck.sw[16])); /* nothing stays held */
    TEST_ASSERT_EQUAL(0, db_sw_deck_pulse(&deck, 17, DB_PRESS_SHORT, true, 1000000, out));
}

static void test_a_fast_spin_makes_ten_presses_a_second(void)
{
    deck = (db_sw_deck_t){0};
    db_sw_event_t out[3];
    int presses = 0;
    for (int64_t t = 1000000; t < 2000000; t += 10000) { /* 100 detents in a second */
        presses += db_sw_deck_pulse(&deck, 8, DB_PRESS_SHORT, false, t, out) > 0;
    }
    TEST_ASSERT_EQUAL(1000000 / DB_SW_PULSE_US, presses);
    TEST_ASSERT_EQUAL(2, db_sw_deck_pulse(&deck, 9, DB_PRESS_SHORT, false, 1990000, out)); /* the other way */
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
    RUN_TEST(test_keys_and_dials_keep_their_switches);
    RUN_TEST(test_turns_and_touches_take_the_free_switches_in_order);
    RUN_TEST(test_the_biggest_touch_deck_fits_its_block);
    RUN_TEST(test_what_does_not_fit_goes_nowhere);
    RUN_TEST(test_a_deck_without_strip_or_dials_has_no_extras);
    RUN_TEST(test_switches_are_named_like_the_web_ui);
    RUN_TEST(test_the_neos_extra_keys_are_touch_points);
    RUN_TEST(test_switches_count_in_deck_order);
    RUN_TEST(test_a_short_press_is_told_at_once);
    RUN_TEST(test_long_press_once_then_long_release);
    RUN_TEST(test_a_repeated_state_is_ignored);
    RUN_TEST(test_a_press_on_the_bridges_own_page_goes_nowhere);
    RUN_TEST(test_a_release_counts_while_covered);
    RUN_TEST(test_the_menu_chord_mutes_both_controls);
    RUN_TEST(test_a_long_press_cut_by_the_chord_ends_long);
    RUN_TEST(test_an_unplug_releases_what_a_controller_saw_held);
    RUN_TEST(test_two_other_controls_together_both_count);
    RUN_TEST(test_a_tap_or_detent_is_a_whole_short_press);
    RUN_TEST(test_a_fast_spin_makes_ten_presses_a_second);
    RUN_TEST(test_only_the_deck_and_the_debug_route_reach_matter);
    return UNITY_END();
}
