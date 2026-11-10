#include <stdio.h>
#include <string.h>
#include <unity.h>

#include "db_dns.h"

void setUp(void)
{
}
void tearDown(void)
{
}

static size_t unhex(const char *hex, uint8_t *out)
{
    size_t n = 0;
    for (; hex[2 * n]; n++) {
        unsigned b;
        sscanf(hex + 2 * n, "%2x", &b);
        out[n] = (uint8_t)b;
    }
    return n;
}

/* A PTR query and answer for 192.168.1.180, a Windows PC. */
static const char *QUERY =
    "123401000001000000000000033138300131033136380331393207696e2d61646472046172706100000c0001";
static const char *ANSWER =
    "123485800001000100000000033138300131033136380331393207696e2d61646472046172706100000c00010331"
    "38300131033136380331393207696e2d61646472046172706100000c00010000000500110f4445534b544f50"
    "2d4558414d504c4500";

static uint32_t ip180(void)
{
    uint8_t a[4] = {192, 168, 1, 180};
    uint32_t ip;
    memcpy(&ip, a, 4);
    return ip;
}

static void test_query_is_what_dig_sends(void)
{
    uint8_t want[64], got[64];
    size_t n = unhex(QUERY, want);
    TEST_ASSERT_EQUAL_size_t(n, db_dns_ptr_query(ip180(), 0x1234, got));
    TEST_ASSERT_EQUAL_MEMORY(want, got, n);
}

static void test_answer_gives_the_host_name(void)
{
    uint8_t msg[256];
    size_t n = unhex(ANSWER, msg);
    char name[DB_DNS_NAME_LEN];
    TEST_ASSERT_TRUE(db_dns_ptr_name(msg, n, 0x1234, name));
    TEST_ASSERT_EQUAL_STRING("DESKTOP-EXAMPLE", name);
    TEST_ASSERT_FALSE(db_dns_ptr_name(msg, n, 0x1235, name)); /* another query's answer */
    for (size_t cut = 0; cut < n; cut++) {
        TEST_ASSERT_FALSE(db_dns_ptr_name(msg, cut, 0x1234, name)); /* cut short: never past the end */
    }
    msg[3] = 0x83; /* NXDOMAIN */
    TEST_ASSERT_FALSE(db_dns_ptr_name(msg, n, 0x1234, name));
}

static void test_compressed_and_looping_names(void)
{
    uint8_t msg[256];
    size_t n = unhex(QUERY, msg);
    msg[2] = 0x81, msg[3] = 0x80, msg[7] = 1;
    const uint8_t rr[] = {0xc0, 12, 0, 12, 0, 1,    0,
                          0,    0,  5, 0,  2, 0xc0, 12}; /* PTR data points at the question */
    memcpy(msg + n, rr, sizeof(rr));
    char name[DB_DNS_NAME_LEN];
    TEST_ASSERT_TRUE(db_dns_ptr_name(msg, n + sizeof(rr), 0x1234, name));
    TEST_ASSERT_EQUAL_STRING("180", name);
    msg[n + 13] = (uint8_t)(n + 12); /* points at itself */
    TEST_ASSERT_FALSE(db_dns_ptr_name(msg, n + sizeof(rr), 0x1234, name));
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_query_is_what_dig_sends);
    RUN_TEST(test_answer_gives_the_host_name);
    RUN_TEST(test_compressed_and_looping_names);
    return UNITY_END();
}
