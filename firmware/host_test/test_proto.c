#include <string.h>
#include <unity.h>

#include "db_json.h"
#include "db_proto.h"

void setUp(void)
{
}

void tearDown(void)
{
}

static void test_input_layout_keeps_sign(void)
{
    db_input_t ev = {.kind = DB_INPUT_ENCODER_TURN, .index = 2, .value = -3, .x = 7, .y2 = 9};
    uint8_t out[DB_INPUT_LEN];
    db_proto_input(out, 1, 0xaabbccdd, 0x0102030405060708ULL, &ev);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(((uint8_t[]){0x83, 1, 0, 0}), out, 4);
    TEST_ASSERT_EQUAL_HEX32(0xaabbccdd, db_get_u32(out + 4));
    TEST_ASSERT_EQUAL_UINT64(0x0102030405060708ULL, db_get_u64(out + 8));
    TEST_ASSERT_EQUAL_UINT8(2, out[16]); /* turn */
    TEST_ASSERT_EQUAL_UINT8(2, out[17]);
    TEST_ASSERT_EQUAL_INT16(-3, (int16_t)db_get_u16(out + 18));
    TEST_ASSERT_EQUAL_UINT16(7, db_get_u16(out + 20));
    TEST_ASSERT_EQUAL_UINT16(9, db_get_u16(out + 26));
    ev.kind = DB_INPUT_KEY_UP;
    db_proto_input(out, 0, 1, 0, &ev);
    TEST_ASSERT_EQUAL_UINT8(1, out[16]);
}

static void test_shown_and_credit_layout(void)
{
    uint8_t s[DB_SHOWN_LEN];
    db_proto_shown(s, 3, true, 0x11223344, 1500);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(((uint8_t[]){0x81, 3, 0, 0, 1, 0x44, 0x33, 0x22, 0x11, 0xdc, 0x05, 0, 0}), s,
                                 sizeof(s));
    uint8_t c[DB_CREDIT_LEN];
    db_proto_credit(c, 0, 1, 0x00200000, 4);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(((uint8_t[]){0x82, 0, 0, 0, 1, 0, 0, 0x20, 0, 4}), c, sizeof(c));
}

static void test_cbor_writes_what_a_reader_finds(void)
{
    uint8_t buf[128];
    db_cbor_t c;
    db_cbor_begin(&c, buf, sizeof(buf));
    db_cbor_map(&c, NULL);
    db_cbor_uint(&c, "n", 1000000);
    db_cbor_arr(&c, "l");
    db_cbor_bool(&c, NULL, true);
    db_cbor_uint(&c, NULL, 23);
    db_cbor_uint(&c, NULL, 24);
    db_cbor_close(&c);
    db_cbor_str(&c, "token", "abc");
    db_cbor_close(&c);
    size_t n = db_cbor_end(&c);
    const uint8_t want[] = {0xbf, 0x61, 'n',  0x1a, 0x00, 0x0f, 0x42, 0x40, 0x61, 'l', 0x9f, 0xf5, 0x17, 0x18,
                            0x18, 0xff, 0x65, 't',  'o',  'k',  'e',  'n',  0x63, 'a', 'b',  'c',  0xff};
    TEST_ASSERT_EQUAL_size_t(sizeof(want), n);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(want, buf, n);
    char out[8];
    TEST_ASSERT_TRUE(db_cbor_text(buf, n, "token", out, sizeof(out)));
    TEST_ASSERT_EQUAL_STRING("abc", out);
    TEST_ASSERT_FALSE(db_cbor_text(buf, n, "token", out, 3)); /* does not fit */
    TEST_ASSERT_FALSE(db_cbor_text(buf, n, "n", out, sizeof(out)));
    TEST_ASSERT_FALSE(db_cbor_text(buf, n - 2, "token", out, sizeof(out)));

    db_cbor_begin(&c, buf, 4);
    db_cbor_str(&c, "key", "too long");
    TEST_ASSERT_EQUAL_size_t(0, db_cbor_end(&c));
}

/* What a host's CBOR library writes: definite lengths, other keys first. */
static void test_cbor_reads_definite_maps(void)
{
    const uint8_t paired[] = {0xa2, 0x61, 'x', 0x82, 0xa1, 0x60, 0xf6, 0x41, 0x00,
                              0x65, 't',  'o', 'k',  'e',  'n',  0x62, 'o',  'k'};
    char out[8];
    TEST_ASSERT_TRUE(db_cbor_text(paired, sizeof(paired), "token", out, sizeof(out)));
    TEST_ASSERT_EQUAL_STRING("ok", out);
    const uint8_t deep[] = {0xa1, 0x61, 'x', 0x81, 0x81, 0x81, 0x81, 0x81, 0x81, 0x81, 0x81, 0x81, 0x81, 0};
    TEST_ASSERT_FALSE(db_cbor_text(deep, sizeof(deep), "token", out, sizeof(out)));
    const uint8_t huge[] = {0xa1, 0x61, 'x', 0x9b, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff};
    TEST_ASSERT_FALSE(db_cbor_text(huge, sizeof(huge), "token", out, sizeof(out)));
}

static void test_json_escapes_and_detects_overflow(void)
{
    char buf[64];
    db_json_t j;
    db_json_begin(&j, buf, sizeof(buf));
    db_json_obj_open(&j, NULL);
    db_json_str(&j, "name", "a\"b\n");
    db_json_int(&j, "n", -5);
    db_json_arr_open(&j, "l");
    db_json_bool(&j, NULL, true);
    db_json_null(&j, NULL);
    db_json_arr_close(&j);
    db_json_obj_close(&j);
    TEST_ASSERT_NOT_EQUAL(0, db_json_end(&j));
    TEST_ASSERT_EQUAL_STRING("{\"name\":\"a\\\"b\\u000a\",\"n\":-5,\"l\":[true,null]}", buf);

    char tiny[8];
    db_json_begin(&j, tiny, sizeof(tiny));
    db_json_str(&j, "key", "too long");
    TEST_ASSERT_EQUAL_size_t(0, db_json_end(&j));
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_input_layout_keeps_sign);
    RUN_TEST(test_shown_and_credit_layout);
    RUN_TEST(test_cbor_writes_what_a_reader_finds);
    RUN_TEST(test_cbor_reads_definite_maps);
    RUN_TEST(test_json_escapes_and_detects_overflow);
    return UNITY_END();
}
