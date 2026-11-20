#include <string.h>

#include "db_client.h"
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

static void test_ipv4(void)
{
    uint32_t v;
    TEST_ASSERT_TRUE(db_ipv4("192.168.100.15", 14, &v));
    TEST_ASSERT_EQUAL_HEX32(ip(192, 168, 100, 15), v);
    TEST_ASSERT_TRUE(db_ipv4("0.0.0.0", 7, &v));
    TEST_ASSERT_TRUE(db_ipv4("255.255.255.255", 15, &v));
    const char *bad[] = {"",           "1.2.3",    "1.2.3.4.5", "256.1.1.1",  "01.2.3.4",
                         "1.2.3.4:80", " 1.2.3.4", "1.2.3.4 ",  "1..2.3",     "a.b.c.d",
                         "::1",        "1.2.3.-4", "1.2.3.4,",  "1234.1.1.1", "1.2.3.4a"};
    for (size_t i = 0; i < sizeof(bad) / sizeof(bad[0]); i++) {
        TEST_ASSERT_FALSE_MESSAGE(db_ipv4(bad[i], strlen(bad[i]), &v), bad[i]);
    }
    TEST_ASSERT_TRUE(db_ipv4("1.2.3.4xyz", 7, &v)); /* only the given length counts */
}

static void test_client(void)
{
    uint32_t proxy = ip(192, 168, 100, 15), peer = ip(192, 168, 100, 20), client = ip(192, 168, 100, 99);
    TEST_ASSERT_EQUAL_HEX32(client, db_client_ip(proxy, proxy, "192.168.100.99"));
    TEST_ASSERT_EQUAL_HEX32(client, db_client_ip(proxy, proxy, "6.6.6.6, 10.0.0.1 , 192.168.100.99 "));
    TEST_ASSERT_EQUAL_HEX32(client, db_client_ip(proxy, proxy, "6.6.6.6,192.168.100.99"));
    /* anyone but the trusted proxy: the header is ignored */
    TEST_ASSERT_EQUAL_HEX32(peer, db_client_ip(peer, proxy, "192.168.100.99"));
    TEST_ASSERT_EQUAL_HEX32(peer, db_client_ip(peer, 0, "192.168.100.99"));
    TEST_ASSERT_EQUAL_HEX32(proxy, db_client_ip(proxy, 0, "192.168.100.99"));
    /* missing or garbage: the peer */
    TEST_ASSERT_EQUAL_HEX32(proxy, db_client_ip(proxy, proxy, NULL));
    TEST_ASSERT_EQUAL_HEX32(proxy, db_client_ip(proxy, proxy, ""));
    TEST_ASSERT_EQUAL_HEX32(proxy, db_client_ip(proxy, proxy, "192.168.100.99, unknown"));
    TEST_ASSERT_EQUAL_HEX32(proxy, db_client_ip(proxy, proxy, "192.168.100.99,"));
    TEST_ASSERT_EQUAL_HEX32(proxy, db_client_ip(proxy, proxy, "2001:db8::1"));
    TEST_ASSERT_EQUAL_HEX32(proxy, db_client_ip(proxy, proxy, "0.0.0.0"));
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_ipv4);
    RUN_TEST(test_client);
    return UNITY_END();
}
