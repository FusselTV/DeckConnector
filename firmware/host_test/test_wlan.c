#include "db_wlan.h"
#include "unity.h"

static db_wlan_t w;
static int64_t now;

void setUp(void)
{
    w = (db_wlan_t){0};
    now = 5000;
    db_wlan_up(&w, now);
}

void tearDown(void)
{
}

/* Disconnects every 2.5 s, as a join that finds no network gives them, until one says switch. */
static int64_t fail_until_switch(void)
{
    int64_t from = now;
    while (!db_wlan_lost(&w, now, true)) {
        now += 2500;
        TEST_ASSERT_LESS_THAN(from + 60000, now);
    }
    return now - from;
}

static void on_backup(void)
{
    fail_until_switch();
    TEST_ASSERT_TRUE(w.backup);
    now += 2000;
    db_wlan_up(&w, now);
}

static void test_a_short_drop_stays_on_the_primary(void)
{
    TEST_ASSERT_FALSE(db_wlan_lost(&w, now, true));
    now += 2500;
    TEST_ASSERT_FALSE(db_wlan_lost(&w, now, true));
    now += 1000;
    db_wlan_up(&w, now);
    now += DB_WLAN_FAILOVER_MS - 1;
    TEST_ASSERT_FALSE(db_wlan_lost(&w, now, true));
    TEST_ASSERT_FALSE(w.backup);
}

static void test_the_backup_after_the_failover_time(void)
{
    int64_t took = fail_until_switch();
    TEST_ASSERT_TRUE(w.backup);
    TEST_ASSERT_GREATER_OR_EQUAL(DB_WLAN_FAILOVER_MS, took);
    TEST_ASSERT_LESS_THAN(DB_WLAN_FAILOVER_MS + 2500, took);
}

static void test_no_backup_set_keeps_trying_the_primary(void)
{
    for (int i = 0; i < 100; i++, now += 2500) {
        TEST_ASSERT_FALSE(db_wlan_lost(&w, now, false));
    }
}

static void test_both_gone_alternates_with_a_full_try_each(void)
{
    TEST_ASSERT_GREATER_OR_EQUAL(DB_WLAN_FAILOVER_MS, fail_until_switch());
    TEST_ASSERT_TRUE(w.backup);
    TEST_ASSERT_GREATER_OR_EQUAL(DB_WLAN_FAILOVER_MS, fail_until_switch());
    TEST_ASSERT_FALSE(w.backup);
}

static void test_on_the_backup_the_primary_is_looked_for_every_wait(void)
{
    on_backup();
    TEST_ASSERT_FALSE(db_wlan_check_due(&w, now + DB_WLAN_CHECK_MS - 1));
    now += DB_WLAN_CHECK_MS;
    TEST_ASSERT_TRUE(db_wlan_check_due(&w, now));
    TEST_ASSERT_FALSE(db_wlan_scanned(&w, now, false));
    TEST_ASSERT_TRUE(w.backup);
    TEST_ASSERT_FALSE(db_wlan_check_due(&w, now + DB_WLAN_CHECK_MS - 1));
    now += DB_WLAN_CHECK_MS;
    TEST_ASSERT_TRUE(db_wlan_scanned(&w, now, true));
    TEST_ASSERT_FALSE(w.backup);
}

static void test_no_look_while_on_the_primary_or_down(void)
{
    TEST_ASSERT_FALSE(db_wlan_check_due(&w, now + 10 * DB_WLAN_CHECK_MS));
    on_backup();
    db_wlan_lost(&w, now, true);
    TEST_ASSERT_FALSE(db_wlan_check_due(&w, now + 10 * DB_WLAN_CHECK_MS));
}

static void test_a_failed_move_back_returns_at_once_and_waits_longer(void)
{
    on_backup();
    now += DB_WLAN_CHECK_MS;
    TEST_ASSERT_TRUE(db_wlan_scanned(&w, now, true));
    now += 2500;
    TEST_ASSERT_TRUE(db_wlan_lost(&w, now, true));
    TEST_ASSERT_TRUE(w.backup);
    db_wlan_up(&w, now + 2000);
    now += 2000;
    TEST_ASSERT_FALSE(db_wlan_check_due(&w, now + 2 * DB_WLAN_CHECK_MS - 1));
    TEST_ASSERT_TRUE(db_wlan_check_due(&w, now + 2 * DB_WLAN_CHECK_MS));
}

static void test_a_primary_that_drops_soon_after_the_move_back_counts_as_failed(void)
{
    on_backup();
    now += DB_WLAN_CHECK_MS;
    db_wlan_scanned(&w, now, true);
    db_wlan_up(&w, now + 3000);
    now += 60000;
    TEST_ASSERT_TRUE(db_wlan_lost(&w, now, true));
    TEST_ASSERT_TRUE(w.backup);
}

static void test_the_wait_doubles_up_to_its_ceiling_and_resets_after_a_lasting_move_back(void)
{
    on_backup();
    for (int i = 0; i < 20; i++) {
        now += DB_WLAN_CHECK_MAX_MS;
        db_wlan_scanned(&w, now, true);
        now += 2500;
        db_wlan_lost(&w, now, true);
        db_wlan_up(&w, now);
    }
    TEST_ASSERT_EQUAL_INT64(DB_WLAN_CHECK_MAX_MS, w.wait_ms);
    now += DB_WLAN_CHECK_MAX_MS;
    db_wlan_scanned(&w, now, true);
    db_wlan_up(&w, now);
    now += DB_WLAN_CHECK_MAX_MS;
    TEST_ASSERT_FALSE(db_wlan_lost(&w, now, true));
    TEST_ASSERT_FALSE(w.backup);
    TEST_ASSERT_EQUAL_INT64(0, w.wait_ms);
}

static void test_a_removed_backup_sends_it_to_the_primary(void)
{
    on_backup();
    TEST_ASSERT_TRUE(db_wlan_lost(&w, now, false));
    TEST_ASSERT_FALSE(w.backup);
    TEST_ASSERT_FALSE(db_wlan_lost(&w, now + 60000, false));
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_a_short_drop_stays_on_the_primary);
    RUN_TEST(test_the_backup_after_the_failover_time);
    RUN_TEST(test_no_backup_set_keeps_trying_the_primary);
    RUN_TEST(test_both_gone_alternates_with_a_full_try_each);
    RUN_TEST(test_on_the_backup_the_primary_is_looked_for_every_wait);
    RUN_TEST(test_no_look_while_on_the_primary_or_down);
    RUN_TEST(test_a_failed_move_back_returns_at_once_and_waits_longer);
    RUN_TEST(test_a_primary_that_drops_soon_after_the_move_back_counts_as_failed);
    RUN_TEST(test_the_wait_doubles_up_to_its_ceiling_and_resets_after_a_lasting_move_back);
    RUN_TEST(test_a_removed_backup_sends_it_to_the_primary);
    return UNITY_END();
}
