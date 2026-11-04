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

static void test_parses_image_header(void)
{
    const uint8_t msg[] = {0x01, 0, 13, 0, 12, 0, 120, 0, 120, 0, 0x78, 0x56, 0x34, 0x12, 0xff, 0xd8};
    db_image_hdr_t h;
    TEST_ASSERT_EQUAL_INT(DB_OK, db_proto_parse_image_hdr(msg, sizeof(msg), &h));
    TEST_ASSERT_EQUAL_UINT16(13, h.rect.x);
    TEST_ASSERT_EQUAL_UINT16(120, h.rect.h);
    TEST_ASSERT_EQUAL_HEX32(0x12345678, h.seq);
}

static void test_rejects_short_or_empty_image(void)
{
    uint8_t msg[DB_IMAGE_HDR_LEN] = {0x01};
    db_image_hdr_t h;
    TEST_ASSERT_EQUAL_INT(DB_ERR_PROTO, db_proto_parse_image_hdr(msg, 5, &h));
    TEST_ASSERT_EQUAL_INT(DB_ERR_PROTO, db_proto_parse_image_hdr(msg, sizeof(msg), &h));
    msg[0] = 0x02;
    TEST_ASSERT_EQUAL_INT(DB_ERR_PROTO, db_proto_parse_image_hdr(msg, sizeof(msg), &h));
}

static void test_ack_layout(void)
{
    uint8_t out[DB_ACK_LEN];
    db_proto_write_ack(out, DB_ACK_SUPERSEDED, 1, 0xaabbccdd, 1, 2, 0x0102030405060708ULL);
    TEST_ASSERT_EQUAL_HEX8(0x81, out[0]);
    TEST_ASSERT_EQUAL_HEX8(1, out[1]);
    TEST_ASSERT_EQUAL_HEX32(0xaabbccdd, db_get_u32(out + 4));
    TEST_ASSERT_EQUAL_UINT64(2, db_get_u64(out + 16));
    TEST_ASSERT_EQUAL_UINT64(0x0102030405060708ULL, db_get_u64(out + 24));
}

static void test_input_layout_keeps_sign(void)
{
    db_input_t ev = {.kind = DB_INPUT_ENCODER_TURN, .index = 2, .value = -3, .t_us = 99};
    uint8_t out[DB_INPUT_LEN];
    db_proto_write_input(out, &ev);
    TEST_ASSERT_EQUAL_HEX8(0x83, out[0]);
    TEST_ASSERT_EQUAL_HEX8(5, out[1]);
    TEST_ASSERT_EQUAL_INT16(-3, (int16_t)db_get_u16(out + 4));
    TEST_ASSERT_EQUAL_UINT64(99, db_get_u64(out + 24));
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

static void test_unplugged_device_json(void)
{
    char buf[64];
    size_t n = db_proto_device_json(NULL, buf, sizeof(buf));
    TEST_ASSERT_EQUAL_STRING("{\"t\":\"device\",\"present\":false}", buf);
    TEST_ASSERT_EQUAL_size_t(strlen(buf), n);
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_parses_image_header);
    RUN_TEST(test_rejects_short_or_empty_image);
    RUN_TEST(test_ack_layout);
    RUN_TEST(test_input_layout_keeps_sign);
    RUN_TEST(test_json_escapes_and_detects_overflow);
    RUN_TEST(test_unplugged_device_json);
    return UNITY_END();
}
