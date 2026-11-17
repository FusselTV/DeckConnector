#include <string.h>

#include "db_device.h"
#include "unity.h"

static db_slot_owner_t owners[DB_MAX_DECKS];
static bool taken[DB_MAX_DECKS];
static int limit;

void setUp(void)
{
    memset(owners, 0, sizeof(owners));
    memset(taken, 0, sizeof(taken));
    limit = DB_MAX_DECKS;
}

void tearDown(void)
{
}

static int take(const char *id)
{
    int slot = db_slot_take(owners, taken, DB_MAX_DECKS, limit, id);
    if (slot >= 0) {
        taken[slot] = true;
    }
    return slot;
}

static void test_new_decks_fill_from_the_first(void)
{
    TEST_ASSERT_EQUAL_INT(0, take("A"));
    TEST_ASSERT_EQUAL_INT(1, take("B"));
    TEST_ASSERT_EQUAL_STRING("A", owners[0]);
    TEST_ASSERT_EQUAL_STRING("B", owners[1]);
}

static void test_a_deck_gets_its_slot_back(void)
{
    take("A");
    take("B");
    taken[1] = false; /* B unplugged and back */
    TEST_ASSERT_EQUAL_INT(1, take("B"));
}

static void test_a_new_deck_leaves_owned_slots_alone(void)
{
    take("A");
    take("B");
    taken[1] = false;
    TEST_ASSERT_EQUAL_INT(2, take("C"));
}

static void test_a_deck_alone_takes_the_first_slot(void)
{
    take("A");
    take("B");
    memset(taken, 0, sizeof(taken)); /* A replaced by a new deck, B gone too */
    TEST_ASSERT_EQUAL_INT(0, take("C"));
    TEST_ASSERT_EQUAL_INT(1, take("B"));
    memset(taken, 0, sizeof(taken));
    TEST_ASSERT_EQUAL_INT(1,
                          take("B")); /* alone, but it owns slot 1: whoever enumerates first keeps its slot */
}

static void test_a_replacement_stays_below_the_limit(void)
{
    limit = 2;
    take("A");
    take("B");
    taken[0] = false; /* A replaced by C while B stays */
    TEST_ASSERT_EQUAL_INT(0, take("C"));
    TEST_ASSERT_EQUAL_STRING("C", owners[0]);
}

static void test_an_owned_slot_over_the_limit_is_not_given(void)
{
    take("A");
    take("B");
    take("C");
    memset(taken, 0, sizeof(taken));
    limit = 2;
    taken[0] = true;
    TEST_ASSERT_EQUAL_INT(1, take("C"));
    TEST_ASSERT_EQUAL_STRING("", owners[2]);
    TEST_ASSERT_EQUAL_INT(-1, take("D"));
}

static void test_all_owned_takes_the_lowest_free(void)
{
    take("A");
    take("B");
    take("C");
    take("D");
    memset(taken, 0, sizeof(taken));
    taken[0] = true;
    TEST_ASSERT_EQUAL_INT(1, take("E"));
    TEST_ASSERT_EQUAL_STRING("E", owners[1]);
}

static void test_own_slot_taken_moves_and_drops_the_old_one(void)
{
    take("A");
    memset(taken, 0, sizeof(taken));
    taken[0] = true; /* another deck sits in A's slot */
    TEST_ASSERT_EQUAL_INT(1, take("A"));
    TEST_ASSERT_EQUAL_STRING("", owners[0]);
    TEST_ASSERT_EQUAL_STRING("A", owners[1]);
}

static void test_full(void)
{
    for (int i = 0; i < DB_MAX_DECKS; i++) {
        taken[i] = true;
    }
    TEST_ASSERT_EQUAL_INT(-1, take("A"));
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_new_decks_fill_from_the_first);
    RUN_TEST(test_a_deck_gets_its_slot_back);
    RUN_TEST(test_a_new_deck_leaves_owned_slots_alone);
    RUN_TEST(test_a_deck_alone_takes_the_first_slot);
    RUN_TEST(test_a_replacement_stays_below_the_limit);
    RUN_TEST(test_an_owned_slot_over_the_limit_is_not_given);
    RUN_TEST(test_all_owned_takes_the_lowest_free);
    RUN_TEST(test_own_slot_taken_moves_and_drops_the_old_one);
    RUN_TEST(test_full);
    return UNITY_END();
}
