#include <stdio.h>
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

static void test_after_a_cycle_a_newcomer_waits_for_the_owners(void)
{
    limit = 2;
    take("A");
    take("B");
    memset(taken, 0, sizeof(taken)); /* all gone in a root port cycle, the parked D enumerates first */
    TEST_ASSERT_TRUE(db_slot_others(owners, taken, limit, "D"));
    TEST_ASSERT_FALSE(db_slot_others(owners, taken, limit, "B"));
    taken[0] = true;
    taken[1] = true;
    /* none free: the limit parks it, not this */
    TEST_ASSERT_FALSE(db_slot_others(owners, taken, limit, "D"));
    limit = 3;
    TEST_ASSERT_FALSE(db_slot_others(owners, taken, limit, "D")); /* slot 2 belongs to nobody */
}

/* Clones that all report one serial, two with a serial of their own that is the same, one without. */
static void test_clones_go_by_their_port_and_get_a_slot_each(void)
{
    db_slot_owner_t present[DB_MAX_DECKS];
    int n = 0;
    const struct {
        const char *serial;
        const char *path;
        const char *id;
    } plugged[] = {{"355499441494", "1.1", "port-1.1"},
                   {"355499441494", "1.2", "port-1.2"},
                   {"ABC", "1.3", "ABC"},
                   {"ABC", "1.4", "port-1.4"}};
    for (int i = 0; i < 4; i++) {
        snprintf(present[n], sizeof(present[n]), "%s", plugged[i].serial);
        db_deck_identity(present[n], plugged[i].path, present, n);
        TEST_ASSERT_EQUAL_STRING(plugged[i].id, present[n]);
        TEST_ASSERT_EQUAL_INT(i, take(present[n++]));
    }
    db_slot_owner_t none = "";
    db_deck_identity(none, "", present, n);
    TEST_ASSERT_EQUAL_STRING("port-root", none);
    db_slot_owner_t elgato = "A00SA0000AA0A0";
    db_deck_identity(elgato, "1.1", present, n);
    TEST_ASSERT_EQUAL_STRING("A00SA0000AA0A0", elgato);
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
    RUN_TEST(test_after_a_cycle_a_newcomer_waits_for_the_owners);
    RUN_TEST(test_all_owned_takes_the_lowest_free);
    RUN_TEST(test_own_slot_taken_moves_and_drops_the_old_one);
    RUN_TEST(test_full);
    RUN_TEST(test_clones_go_by_their_port_and_get_a_slot_each);
    return UNITY_END();
}
