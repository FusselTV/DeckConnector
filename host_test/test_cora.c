#include <string.h>
#include <unity.h>

#include "db_cora.h"

static uint8_t hex_nibble(char c)
{
    return (uint8_t)(c <= '9' ? c - '0' : c - 'a' + 10);
}

/* Compare the start of `got` with the expected hex. */
static void assert_prefix(const char *hex, const uint8_t *got)
{
    size_t n = strlen(hex) / 2;
    uint8_t want[64];
    for (size_t i = 0; i < n; i++) {
        want[i] = (uint8_t)(hex_nibble(hex[2 * i]) << 4 | hex_nibble(hex[2 * i + 1]));
    }
    TEST_ASSERT_EQUAL_HEX8_ARRAY(want, got, n);
}

static db_cora_dock_t dock = {.serial = "AB00A00000AAAA", .mac = {0x80, 0x7b, 0x1e, 0x00, 0x00, 0x01}};

void setUp(void)
{
    dock.device = NULL;
}

void tearDown(void)
{
}

static void test_header_round_trip(void)
{
    uint8_t buf[DB_CORA_HDR_LEN];
    db_cora_hdr_t h = {
        .flags = DB_CORA_FLAG_VERBATIM, .op = DB_CORA_OP_GET_REPORT, .id = 0x1234, .len = 1024};
    db_cora_write_hdr(buf, &h);
    assert_prefix("43938a4100800200341200000004000", buf);
    db_cora_hdr_t back;
    TEST_ASSERT_EQUAL_INT(DB_OK, db_cora_parse_hdr(buf, sizeof(buf), &back));
    TEST_ASSERT_EQUAL_HEX16(0x8000, back.flags);
    TEST_ASSERT_EQUAL_UINT32(1024, back.len);
    TEST_ASSERT_EQUAL_INT(DB_ERR_STATE, db_cora_parse_hdr(buf, 15, &back));
    buf[0] = 0;
    TEST_ASSERT_EQUAL_INT(DB_ERR_PROTO, db_cora_parse_hdr(buf, sizeof(buf), &back));
}

static void test_reports_match_protocol(void)
{
    uint8_t out[DB_CORA_REPORT_LEN];
    db_cora_dock_report(0x80, &dock, out);
    assert_prefix("038000000000000000000000d90fffff", out);
    db_cora_dock_report(0x83, &dock, out);
    assert_prefix("0383000000000000312e30312e303134", out + 0); /* checksum bytes are zero here */
    db_cora_dock_report(0x84, &dock, out);
    assert_prefix("0384000e4142303041303030303041414141", out);
    db_cora_dock_report(0x85, &dock, out);
    assert_prefix("03850600807b1e000001", out);
    db_cora_dock_report(0x1c, &dock, out);
    assert_prefix("010b7c0001", out);
    db_cora_keepalive(1, out);
    assert_prefix("010a020002010000", out);
}

static void test_attached_device_info(void)
{
    db_device_info_t dev = {.vid = 0x0fd9, .pid = 0x0084, .serial = "EL00K1A00001"};
    dock.device = &dev;
    uint8_t out[DB_CORA_REPORT_LEN];
    db_cora_dock_report(0x1c, &dock, out);
    TEST_ASSERT_EQUAL_HEX8(0x02, out[4]);
    TEST_ASSERT_EQUAL_HEX16(0x0fd9, db_get_u16(out + 26));
    TEST_ASSERT_EQUAL_HEX16(0x0084, db_get_u16(out + 28));
    TEST_ASSERT_EQUAL_STRING("EL00K1A00001", (const char *)out + 94);
    TEST_ASSERT_EQUAL_UINT16(5344, db_get_u16(out + 126));
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_header_round_trip);
    RUN_TEST(test_reports_match_protocol);
    RUN_TEST(test_attached_device_info);
    return UNITY_END();
}
