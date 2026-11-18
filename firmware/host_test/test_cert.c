#include <string.h>

#include "db_cert.h"
#include "unity.h"

void setUp(void)
{
}

void tearDown(void)
{
}

static uint32_t ip(uint8_t a, uint8_t b, uint8_t c, uint8_t d)
{
    uint8_t raw[4] = {a, b, c, d};
    uint32_t v;
    memcpy(&v, raw, 4);
    return v;
}

static void test_names(void)
{
    char out[128];
    uint32_t ips[] = {ip(192, 168, 1, 20), 0,
                      ip(10, 0, 0, 5),     ip(192, 168, 1, 20),
                      ip(8, 8, 8, 8),      ip(172, 16, 0, 1),
                      ip(172, 32, 0, 1),   ip(169, 254, 3, 4),
                      ip(100, 64, 0, 1)};
    TEST_ASSERT_TRUE(db_cert_names("deck", ips, 9, out, sizeof(out)));
    TEST_ASSERT_EQUAL_STRING("deck.local,localhost,192.168.1.20,10.0.0.5,172.16.0.1,169.254.3.4", out);
    TEST_ASSERT_TRUE(db_cert_names("deck", NULL, 0, out, sizeof(out)));
    TEST_ASSERT_EQUAL_STRING("deck.local,localhost", out);
    TEST_ASSERT_FALSE(db_cert_names("deck", ips, 3, out, 30));
}

static void test_renew(void)
{
    const char *have = "deck.local,localhost,192.168.1.20,10.0.0.5";
    int64_t now = 1800000000, until = now + 400LL * 86400;
    TEST_ASSERT_FALSE(db_cert_renew(have, have, now, until));
    TEST_ASSERT_FALSE(db_cert_renew(have, "deck.local,localhost,10.0.0.5", now, until));
    TEST_ASSERT_FALSE(db_cert_renew(have, "DECK.local,localhost", now, until));
    TEST_ASSERT_TRUE(db_cert_renew(have, "deck.local,localhost,192.168.1.21", now, until));
    TEST_ASSERT_TRUE(db_cert_renew(have, "desk.local,localhost,10.0.0.5", now, until));
    TEST_ASSERT_TRUE(db_cert_renew(have, "deck2.local,localhost", now, until));
    TEST_ASSERT_TRUE(db_cert_renew(have, "deck.local,localhost,10.0.0.50", now, until));
    TEST_ASSERT_TRUE(db_cert_renew("", "deck.local,localhost", now, until));
    TEST_ASSERT_TRUE(db_cert_renew(have, have, now, now + 29LL * 86400));
    TEST_ASSERT_FALSE(db_cert_renew(have, have, now, now + 31LL * 86400));
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_names);
    RUN_TEST(test_renew);
    return UNITY_END();
}
