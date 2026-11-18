#include <string.h>

#include "db_cert.h"
#include "unity.h"

void setUp(void)
{
}

void tearDown(void)
{
}

static void test_names(void)
{
    char out[64];
    TEST_ASSERT_TRUE(db_cert_names("deck", out, sizeof(out)));
    TEST_ASSERT_EQUAL_STRING("deck.local,localhost", out);
    TEST_ASSERT_FALSE(db_cert_names("deck", out, 20));
}

static void test_constraints_name_only(void)
{
    uint8_t want[81] = {0x30, 0x4f, 0xa0, 0x1b, 0x30, 0x0c, 0x82, 0x0a, 'd',  'e',  'c', 'k', '.',
                        'l',  'o',  'c',  'a',  'l',  0x30, 0x0b, 0x82, 0x09, 'l',  'o', 'c', 'a',
                        'l',  'h',  'o',  's',  't',  0xa1, 0x30, 0x30, 0x0a, 0x87, 0x08};
    memcpy(want + 45, (uint8_t[]){0x30, 0x22, 0x87, 0x20},
           4); /* zeros: address and mask of all IPv4, all IPv6 */
    size_t len = sizeof(want);
    uint8_t out[128];
    TEST_ASSERT_EQUAL(len, db_cert_constraints("deck", out, sizeof(out)));
    TEST_ASSERT_EQUAL_HEX8_ARRAY(want, out, len);
    TEST_ASSERT_EQUAL(0, db_cert_constraints("deck", out, len - 1));
    char longest[32];
    memset(longest, 'a', 31);
    longest[31] = '\0';
    size_t n = db_cert_constraints(longest, out, sizeof(out));
    TEST_ASSERT_EQUAL(2 + 2 + (4 + 37 + 4 + 9) + 2 + 48, n);
    TEST_ASSERT_EQUAL(n - 2, out[1]);
}

static void test_renew(void)
{
    const char *have = "deck.local,localhost";
    int64_t now = 1800000000, until = now + 400LL * 86400;
    TEST_ASSERT_FALSE(db_cert_renew(have, have, now, until));
    TEST_ASSERT_FALSE(db_cert_renew(have, "DECK.local,localhost", now, until));
    TEST_ASSERT_TRUE(db_cert_renew(have, "desk.local,localhost", now, until));
    TEST_ASSERT_TRUE(db_cert_renew("", have, now, until));
    TEST_ASSERT_TRUE(db_cert_renew(have, have, now, now + 29LL * 86400));
    TEST_ASSERT_FALSE(db_cert_renew(have, have, now, now + 31LL * 86400));
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_names);
    RUN_TEST(test_constraints_name_only);
    RUN_TEST(test_renew);
    return UNITY_END();
}
