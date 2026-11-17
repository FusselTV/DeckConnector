#include "db_version.h"
#include "unity.h"

void setUp(void)
{
}

void tearDown(void)
{
}

static void test_order(void)
{
    const char *up[] = {
        "not a version", "v0.9.9", "v1.0.0-rc2",        "v1.0.0-rc3", "v1.0.0-rc3-1-g9925b3f-dirty",
        "v1.0.0-rc10",   "v1.0.0", "v1.0.0-4-gabcdef0", "v1.0.1",     "v1.1.0",
        "v2.0.0",
    };
    for (size_t i = 0; i < sizeof(up) / sizeof(up[0]); i++) {
        for (size_t j = 0; j < sizeof(up) / sizeof(up[0]); j++) {
            int c = db_version_cmp(up[i], up[j]);
            int want = i < j ? -1 : i > j;
            TEST_ASSERT_EQUAL_INT_MESSAGE(want, c < 0 ? -1 : c > 0, up[i]);
        }
    }
}

static void test_dirty_and_prerelease(void)
{
    TEST_ASSERT_EQUAL_INT(0, db_version_cmp("v1.0.0-dirty", "v1.0.0")); /* a local build of the release */
    TEST_ASSERT_TRUE(db_version_pre("v1.0.0-rc3"));
    TEST_ASSERT_TRUE(db_version_pre("v1.0.0-rc3-1-g9925b3f-dirty"));
    TEST_ASSERT_FALSE(db_version_pre("v1.0.0"));
    TEST_ASSERT_FALSE(db_version_pre("v1.0.0-4-gabcdef0"));
    TEST_ASSERT_FALSE(db_version_pre("v1.0.0-dirty"));
    TEST_ASSERT_FALSE(db_version_pre("v1.0.0-4-gdeadbee"));
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_order);
    RUN_TEST(test_dirty_and_prerelease);
    return UNITY_END();
}
