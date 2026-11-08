#include <string.h>

#include "db_ptable.h"
#include "unity.h"

#define FLASH (32u << 20)
#define IMAGE 1500000u

typedef struct {
    uint8_t type, subtype;
    uint32_t offset, size;
    const char *label;
} row_t;

/* firmware/partitions.csv */
static const row_t ours[] = {
    {1, 2, 0x9000, 0x6000, "nvs"},        {1, 0, 0xf000, 0x2000, "otadata"},
    {1, 1, 0x11000, 0x1000, "phy_init"},  {0, 16, 0x20000, 0x400000, "ota_0"},
    {0, 17, 0x420000, 0x400000, "ota_1"}, {1, 3, 0x820000, 0x40000, "coredump"},
    {0, 18, 0x860000, 0x400000, "ota_2"}, {0, 19, 0xc60000, 0x3a0000, "ota_3"},
    {1, 64, 0x1000000, 0x100000, "www"},
};
#define ROWS (sizeof(ours) / sizeof(ours[0]))

static uint8_t now[3072];
static uint8_t next[3072];

static void put32(uint8_t *p, uint32_t v)
{
    for (int i = 0; i < 4; i++) {
        p[i] = (uint8_t)(v >> (8 * i));
    }
}

static void write_table(uint8_t *t, const row_t *rows, size_t n)
{
    memset(t, 0xff, 3072);
    for (size_t i = 0; i < n; i++) {
        uint8_t *e = t + 32 * i;
        memset(e, 0, 32);
        e[0] = 0xAA;
        e[1] = 0x50;
        e[2] = rows[i].type;
        e[3] = rows[i].subtype;
        put32(e + 4, rows[i].offset);
        put32(e + 8, rows[i].size);
        strncpy((char *)e + 12, rows[i].label, 16);
    }
}

static const char *with(size_t index, row_t changed)
{
    row_t rows[ROWS];
    memcpy(rows, ours, sizeof(ours));
    rows[index] = changed;
    write_table(next, rows, ROWS);
    return db_ptable_check(now, next, sizeof(now), 0x20000, IMAGE, FLASH);
}

void setUp(void)
{
    write_table(now, ours, ROWS);
}

void tearDown(void)
{
}

static void test_the_same_table_may_be_written(void)
{
    write_table(next, ours, ROWS);
    TEST_ASSERT_NULL(db_ptable_check(now, next, sizeof(now), 0x20000, IMAGE, FLASH));
}

static void test_a_new_data_partition_in_free_space_is_fine(void)
{
    row_t rows[ROWS + 1];
    memcpy(rows, ours, sizeof(ours));
    rows[ROWS] = (row_t){1, 0x81, 0x1100000, 0x100000, "extra"};
    write_table(next, rows, ROWS + 1);
    TEST_ASSERT_NULL(db_ptable_check(now, next, sizeof(now), 0x20000, IMAGE, FLASH));
}

static void test_settings_and_the_page_stay_where_they_are(void)
{
    TEST_ASSERT_NOT_NULL(with(0, (row_t){1, 2, 0xA000, 0x6000, "nvs"}));
    TEST_ASSERT_NOT_NULL(with(0, (row_t){1, 2, 0x9000, 0x5000, "nvs"}));
    TEST_ASSERT_NOT_NULL(with(1, (row_t){1, 0, 0xf000, 0x2000, "otadata2"}));
    TEST_ASSERT_NOT_NULL(with(8, (row_t){1, 64, 0x1000000, 0x80000, "www"}));
}

static void test_app_slots_keep_the_running_one_and_fit_it(void)
{
    TEST_ASSERT_NOT_NULL(with(3, (row_t){0, 16, 0x30000, 0x3f0000, "ota_0"}));  /* the running one moves */
    TEST_ASSERT_NOT_NULL(with(7, (row_t){0, 19, 0xc60000, 0x100000, "ota_3"})); /* too small */
    TEST_ASSERT_NOT_NULL(with(7, (row_t){0, 19, 0xc68000, 0x390000, "ota_3"})); /* unaligned */
    TEST_ASSERT_NULL(with(7, (row_t){0, 19, 0xc60000, 0x300000, "ota_3"}));     /* smaller but still fits */
}

static void test_overlap_flash_end_and_garbage_are_refused(void)
{
    TEST_ASSERT_NOT_NULL(with(7, (row_t){0, 19, 0xc50000, 0x3a0000, "ota_3"})); /* into ota_2 */
    TEST_ASSERT_NOT_NULL(with(8, (row_t){1, 64, 0x1f80000, 0x100000, "www"}));  /* past 32 MB, and moved */
    memset(next, 0xff, sizeof(next));
    TEST_ASSERT_NOT_NULL(db_ptable_check(now, next, sizeof(now), 0x20000, IMAGE, FLASH));
}

static void test_nothing_may_sit_on_the_bootloader_or_the_table(void)
{
    row_t rows[ROWS + 1];
    memcpy(rows, ours, sizeof(ours));
    rows[ROWS] = (row_t){1, 0x81, 0x4000, 0x1000, "spare"}; /* data in the bootloader */
    write_table(next, rows, ROWS + 1);
    TEST_ASSERT_NOT_NULL(db_ptable_check(now, next, sizeof(now), 0x20000, IMAGE, FLASH));
    rows[ROWS] = (row_t){0, 0x10, 0x0, 0x10000, "ota_x"}; /* an app slot at 0 */
    write_table(next, rows, ROWS + 1);
    TEST_ASSERT_NOT_NULL(db_ptable_check(now, next, sizeof(now), 0x20000, IMAGE, FLASH));
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_the_same_table_may_be_written);
    RUN_TEST(test_a_new_data_partition_in_free_space_is_fine);
    RUN_TEST(test_settings_and_the_page_stay_where_they_are);
    RUN_TEST(test_app_slots_keep_the_running_one_and_fit_it);
    RUN_TEST(test_overlap_flash_end_and_garbage_are_refused);
    RUN_TEST(test_nothing_may_sit_on_the_bootloader_or_the_table);
    return UNITY_END();
}
