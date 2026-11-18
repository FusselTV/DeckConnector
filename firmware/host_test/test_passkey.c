#include <string.h>

#include "db_passkey.h"
#include "unity.h"

void setUp(void)
{
}

void tearDown(void)
{
}

static void test_authdata(void)
{
    uint8_t ad[80] = {0};
    memset(ad, 0xab, 32);
    ad[32] = DB_AD_UP | DB_AD_UV;
    ad[33] = 0x01, ad[36] = 0x02;
    db_authdata_t a;
    TEST_ASSERT_TRUE(db_authdata(ad, 37, &a));
    TEST_ASSERT_EQUAL_PTR(ad, a.rp_hash);
    TEST_ASSERT_EQUAL_HEX32(0x01000002, a.count);
    TEST_ASSERT_NULL(a.cred);
    TEST_ASSERT_FALSE(db_authdata(ad, 36, &a));

    ad[32] |= DB_AD_AT;
    ad[54] = 16;
    TEST_ASSERT_TRUE(db_authdata(ad, 55 + 16, &a));
    TEST_ASSERT_EQUAL_PTR(ad + 55, a.cred);
    TEST_ASSERT_EQUAL(16, a.cred_len);
    TEST_ASSERT_FALSE(db_authdata(ad, 55 + 15, &a));
    TEST_ASSERT_FALSE(db_authdata(ad, 54, &a));
    ad[54] = 0;
    TEST_ASSERT_FALSE(db_authdata(ad, 80, &a));
}

static void test_spki(void)
{
    uint8_t der[91] = {0x30, 0x59, 0x30, 0x13, 0x06, 0x07, 0x2a, 0x86, 0x48, 0xce, 0x3d, 0x02, 0x01, 0x06,
                       0x08, 0x2a, 0x86, 0x48, 0xce, 0x3d, 0x03, 0x01, 0x07, 0x03, 0x42, 0x00, 0x04};
    for (int i = 27; i < 91; i++) {
        der[i] = (uint8_t)i;
    }
    uint8_t xy[64];
    TEST_ASSERT_TRUE(db_spki_p256(der, sizeof(der), xy));
    TEST_ASSERT_EQUAL_UINT8_ARRAY(der + 27, xy, 64);
    TEST_ASSERT_FALSE(db_spki_p256(der, 90, xy));
    der[22] = 0x22; /* secp384r1 */
    TEST_ASSERT_FALSE(db_spki_p256(der, sizeof(der), xy));
}

static void test_count(void)
{
    TEST_ASSERT_TRUE(db_count_ok(0, 0));
    TEST_ASSERT_TRUE(db_count_ok(4, 5));
    TEST_ASSERT_FALSE(db_count_ok(5, 5));
    TEST_ASSERT_FALSE(db_count_ok(5, 4));
    TEST_ASSERT_FALSE(db_count_ok(5, 0));
}

static void test_b64url(void)
{
    uint8_t out[8];
    TEST_ASSERT_EQUAL(3, db_b64url("-_-_", out, sizeof(out)));
    TEST_ASSERT_EQUAL_HEX8_ARRAY(((uint8_t[]){0xfb, 0xff, 0xbf}), out, 3);
    TEST_ASSERT_EQUAL(5, db_b64url("aGVsbG8", out, sizeof(out)));
    TEST_ASSERT_EQUAL_MEMORY("hello", out, 5);
    TEST_ASSERT_EQUAL(5, db_b64url("aGVsbG8=", out, sizeof(out)));
    TEST_ASSERT_EQUAL(0, db_b64url("", out, sizeof(out)));
    TEST_ASSERT_EQUAL(-1, db_b64url("aGVs+G8", out, sizeof(out)));
    TEST_ASSERT_EQUAL(-1, db_b64url("a", out, sizeof(out)));
    TEST_ASSERT_EQUAL(-1, db_b64url("aGVsbG8", out, 4));
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_authdata);
    RUN_TEST(test_spki);
    RUN_TEST(test_count);
    RUN_TEST(test_b64url);
    return UNITY_END();
}
