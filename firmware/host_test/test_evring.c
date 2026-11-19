#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "db_evring.h"
#include "unity.h"

#define SIZE  (512u * 1024)
#define SECTS (SIZE / DB_EVRING_ERASE)
#define SLOTS (SIZE / DB_EVREC_SIZE)

/* NOR flash: erase sets a sector to 0xFF, a write only clears bits. Writing over a used byte is a test
 * failure. */
static uint8_t flash[SIZE];
static uint32_t erases[SECTS];
static size_t torn_at;     /* >0: the next write stops after this many bytes, as at a power cut */
static int cut_after = -1; /* writes and erases left before the power goes, -1: it stays */
static bool dead;
static uint32_t acked[SLOTS]; /* the seq an append that returned 0 left in each slot */
static uint8_t buf[DB_EVRING_ERASE];

static bool power_cut(void)
{
    if (cut_after >= 0 && cut_after-- == 0) {
        dead = true;
        return true;
    }
    return false;
}

static int rd(uint32_t off, void *out, size_t len)
{
    memcpy(out, flash + off, len);
    return 0;
}

static int wr(uint32_t off, const void *in, size_t len)
{
    if (dead) {
        return -1;
    }
    for (size_t i = 0; i < len; i++) {
        if (flash[off + i] != 0xFF) {
            TEST_FAIL_MESSAGE("written over a used byte");
        }
    }
    size_t n = power_cut() ? (size_t)rand() % len : torn_at ? torn_at : len;
    torn_at = 0;
    for (size_t i = 0; i < n; i++) {
        flash[off + i] &= ((const uint8_t *)in)[i];
    }
    return n == len ? 0 : -1;
}

static int er(uint32_t off, size_t len)
{
    TEST_ASSERT_EQUAL(0, off % DB_EVRING_ERASE);
    if (dead) {
        return -1;
    }
    if (power_cut()) { /* some bytes are erased already */
        static uint8_t was[DB_EVRING_ERASE];
        memcpy(was, flash + off, len);
        for (size_t i = (size_t)rand() % len; i--;) {
            flash[off + (size_t)rand() % len] = 0xFF;
        }
        for (size_t i = 0; i < len; i += DB_EVREC_SIZE) {
            if (memcmp(was + i, flash + off + i, DB_EVREC_SIZE)) {
                acked[(off + i) / DB_EVREC_SIZE] = 0;
            }
        }
        return -1;
    }
    memset(&acked[off / DB_EVREC_SIZE], 0, len / DB_EVREC_SIZE * sizeof(acked[0]));
    memset(flash + off, 0xFF, len);
    erases[off / DB_EVRING_ERASE]++;
    return 0;
}

static const db_evflash_t F = {.read = rd, .write = wr, .erase = er, .size = SIZE};
static db_evring_t r;

void setUp(void)
{
    memset(flash, 0xFF, sizeof(flash));
    memset(erases, 0, sizeof(erases));
    torn_at = 0;
    cut_after = -1;
    dead = false;
    TEST_ASSERT_EQUAL(0, db_evring_open(&r, &F, buf));
}

void tearDown(void)
{
}

static int put(uint32_t seq)
{
    char n[12];
    snprintf(n, sizeof(n), "%u", (unsigned)seq);
    db_evrec_t e = {.seq = seq, .unix_s = 1700000000 + seq, .since_s = seq, .boot = 3, .code = 7, .kind = 2};
    db_evrec_set_args(&e, (const char *const[]){"192.168.188.15", n, NULL});
    return db_evring_append(&r, &e);
}

static void reopen(void)
{
    TEST_ASSERT_EQUAL(0, db_evring_open(&r, &F, buf));
}

static void test_records_come_back_with_their_fields(void)
{
    TEST_ASSERT_EQUAL(1, r.next);
    TEST_ASSERT_EQUAL(0, r.oldest);
    for (uint32_t s = 1; s <= 100; s++) {
        TEST_ASSERT_EQUAL(0, put(s));
    }
    db_evrec_t e;
    TEST_ASSERT_TRUE(db_evring_get(&r, 42, &e));
    TEST_ASSERT_EQUAL(1700000042, e.unix_s);
    TEST_ASSERT_EQUAL(3, e.boot);
    TEST_ASSERT_EQUAL_STRING("192.168.188.15", db_evrec_arg(&e, 0));
    TEST_ASSERT_EQUAL_STRING("42", db_evrec_arg(&e, 1));
    TEST_ASSERT_NULL(db_evrec_arg(&e, 2));
    TEST_ASSERT_FALSE(db_evring_get(&r, 101, &e));
    TEST_ASSERT_EQUAL(1, r.oldest);
    TEST_ASSERT_EQUAL(100, r.newest);
}

static void test_a_restart_continues_after_the_newest(void)
{
    for (uint32_t s = 1; s <= 70; s++) {
        put(s);
    }
    reopen();
    TEST_ASSERT_EQUAL(71, r.next);
    TEST_ASSERT_EQUAL(1, r.oldest);
    TEST_ASSERT_EQUAL(0, put(71));
    TEST_ASSERT_EQUAL(-1, put(50)); /* an old number is refused, never written over */
}

static void test_the_ring_wraps_and_keeps_all_but_one_sector(void)
{
    uint32_t last = 3 * SLOTS + 17;
    for (uint32_t s = 1; s <= last; s++) {
        TEST_ASSERT_EQUAL(0, put(s));
    }
    db_evrec_t e;
    uint32_t kept = 0;
    for (uint32_t s = 1; s <= last; s++) {
        kept += db_evring_get(&r, s, &e);
    }
    TEST_ASSERT_EQUAL((SECTS - 2) * (DB_EVRING_ERASE / DB_EVREC_SIZE) + 18,
                      kept); /* slots 0 to 17 of the newest sector */
    TEST_ASSERT_TRUE(db_evring_get(&r, r.oldest, &e));
    TEST_ASSERT_FALSE(db_evring_get(&r, r.oldest - 1, &e));
    uint32_t newest = r.newest;
    uint32_t oldest = r.oldest;
    reopen();
    TEST_ASSERT_EQUAL(newest, r.newest);
    TEST_ASSERT_EQUAL(oldest, r.oldest);
    TEST_ASSERT_EQUAL(0, put(last + 1));
}

static void test_a_record_cut_by_a_power_cut_is_skipped(void)
{
    for (uint32_t s = 1; s <= 10; s++) {
        put(s);
    }
    torn_at = 20;
    TEST_ASSERT_NOT_EQUAL(0, put(11));
    reopen();
    TEST_ASSERT_EQUAL(10, r.newest);
    TEST_ASSERT_EQUAL(12, r.next); /* slot 11 holds half a record: never written again before its erase */
    db_evrec_t e;
    TEST_ASSERT_FALSE(db_evring_get(&r, 11, &e));
    TEST_ASSERT_EQUAL(0, put(12));
    TEST_ASSERT_TRUE(db_evring_get(&r, 12, &e));
}

static void test_a_power_cut_in_the_last_slot_of_a_sector(void)
{
    uint32_t per = DB_EVRING_ERASE / DB_EVREC_SIZE;
    for (uint32_t s = 1; s < per - 1; s++) {
        put(s);
    }
    torn_at = 5;
    put(per - 1);
    reopen();
    TEST_ASSERT_EQUAL(per, r.next);
    TEST_ASSERT_EQUAL(0, put(per));
    TEST_ASSERT_EQUAL(0, put(per + 1));
}

static void test_old_data_in_a_new_partition_is_ignored(void)
{
    srand(7);
    for (size_t i = 0; i < SIZE; i++) {
        flash[i] = (uint8_t)rand();
    }
    reopen();
    TEST_ASSERT_EQUAL(0, r.oldest);
    uint32_t from = r.next;
    for (uint32_t s = from; s < from + 3 * SLOTS / 2; s++) {
        TEST_ASSERT_EQUAL(0, put(s));
    }
    db_evrec_t e;
    TEST_ASSERT_TRUE(db_evring_get(&r, r.newest, &e));
}

static void test_skipped_numbers_stay_empty(void)
{
    put(1);
    TEST_ASSERT_EQUAL(0, put(5));
    TEST_ASSERT_EQUAL(0, put(5 + 2 * DB_EVRING_ERASE / DB_EVREC_SIZE)); /* two sectors on */
    db_evrec_t e;
    TEST_ASSERT_FALSE(db_evring_get(&r, 3, &e));
    TEST_ASSERT_TRUE(db_evring_get(&r, 5, &e));
    reopen();
    TEST_ASSERT_EQUAL(6 + 2 * DB_EVRING_ERASE / DB_EVREC_SIZE, r.next);
}

/* 16 sectors, numbers that jump now and then, and the power going at a random write or erase: every record an
 * append confirmed comes back, and nothing is written over (wr). */
static void test_power_cuts_lose_only_what_was_not_confirmed(void)
{
    const db_evflash_t small = {.read = rd, .write = wr, .erase = er, .size = 16 * DB_EVRING_ERASE};
    const uint32_t slots = small.size / DB_EVREC_SIZE;
    memset(acked, 0, sizeof(acked));
    srand(11);
    for (int round = 0; round < 1000; round++) {
        dead = false;
        cut_after = rand() % 3 ? rand() % 200 : -1;
        if (db_evring_open(&r, &small, buf) != 0) {
            TEST_ASSERT_TRUE(dead);
            continue;
        }
        uint32_t newest = 0;
        for (uint32_t s = 0; s < slots; s++) {
            db_evrec_t e;
            if (acked[s]) {
                TEST_ASSERT_TRUE(db_evring_get(&r, acked[s], &e));
                newest = acked[s] > newest ? acked[s] : newest;
            }
        }
        TEST_ASSERT_GREATER_THAN(newest, r.next);
        uint32_t seq = r.next;
        for (int n = rand() % 200; n-- && !dead; seq++) {
            seq += rand() % 50 ? 0 : (uint32_t)rand() % (2 * slots);
            if (put(seq) == 0) {
                acked[seq % slots] = seq;
            }
        }
    }
}

static void test_args_that_do_not_fit_are_cut(void)
{
    db_evrec_t e = {0};
    db_evrec_set_args(
        &e, (const char *const[]){"Fussel-IoT", "7c:10:c9:aa:01:02", "11", "7c:10:c9:aa:01:01", NULL});
    TEST_ASSERT_EQUAL_STRING("Fussel-IoT", db_evrec_arg(&e, 0));
    TEST_ASSERT_EQUAL_STRING("11", db_evrec_arg(&e, 2));
    TEST_ASSERT_EQUAL_STRING("7c:10:c9:aa:0", db_evrec_arg(&e, 3));
    TEST_ASSERT_NULL(db_evrec_arg(&e, 4));
    db_evrec_set_args(&e, (const char *const[]){NULL});
    TEST_ASSERT_NULL(db_evrec_arg(&e, 0));
}

static uint32_t most_erases_in_a_year(uint32_t per_day)
{
    memset(erases, 0, sizeof(erases));
    uint32_t n = per_day * 365;
    for (uint32_t s = r.next; n--; s++) {
        TEST_ASSERT_EQUAL(0, put(s));
    }
    uint32_t max = 0;
    for (size_t i = 0; i < SECTS; i++) {
        max = erases[i] > max ? erases[i] : max;
    }
    printf("wear: %u events a day for a year, at most %u erases per sector (rated 100000)\n",
           (unsigned)per_day, (unsigned)max);
    return max;
}

/* A year at 40 events a day, and at the write cap of 600 an hour: the most erases any sector sees. */
static void test_wear_in_a_year(void)
{
    TEST_ASSERT_LESS_THAN(10, most_erases_in_a_year(40));
    TEST_ASSERT_LESS_THAN(100000 / 20, most_erases_in_a_year(600 * 24));
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_records_come_back_with_their_fields);
    RUN_TEST(test_a_restart_continues_after_the_newest);
    RUN_TEST(test_the_ring_wraps_and_keeps_all_but_one_sector);
    RUN_TEST(test_a_record_cut_by_a_power_cut_is_skipped);
    RUN_TEST(test_a_power_cut_in_the_last_slot_of_a_sector);
    RUN_TEST(test_old_data_in_a_new_partition_is_ignored);
    RUN_TEST(test_skipped_numbers_stay_empty);
    RUN_TEST(test_power_cuts_lose_only_what_was_not_confirmed);
    RUN_TEST(test_args_that_do_not_fit_are_cut);
    RUN_TEST(test_wear_in_a_year);
    return UNITY_END();
}
