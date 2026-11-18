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
    TEST_ASSERT_TRUE(n <= 128);
    uint8_t want[128];
    for (size_t i = 0; i < n; i++) {
        want[i] = (uint8_t)(hex_nibble(hex[2 * i]) << 4 | hex_nibble(hex[2 * i + 1]));
    }
    TEST_ASSERT_EQUAL_HEX8_ARRAY(want, got, n);
}

static db_cora_dock_t dock = {
    .serial = "AB00A00000AAAA", .mac = {0x80, 0x7b, 0x1e, 0x00, 0x00, 0x01}, .device_port = 5344};

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
    assert_prefix("0383000025e49096312e30312e303134", out);
    db_cora_dock_report(0x8f, &dock, out);
    assert_prefix("038f020002", out); /* nothing attached */
    TEST_ASSERT_TRUE(db_cora_is_query(0x8b));
    TEST_ASSERT_FALSE(db_cora_is_query(0x1a)); /* keepalive ack */
    db_cora_dock_report(0x84, &dock, out);
    assert_prefix("0384000e4142303041303030303041414141", out);
    db_cora_dock_report(0x85, &dock, out);
    assert_prefix("03850600807b1e000001", out);
    db_cora_dock_report(0x1c, &dock, out);
    assert_prefix("010b7c0001", out);
    db_cora_keepalive(1, out);
    assert_prefix("010a020001010000", out);
}

static void test_attached_device_info(void)
{
    db_device_info_t dev = {.vendor = "Elgato",
                            .vid = 0x0fd9,
                            .pid = 0x0084,
                            .serial = "EL00K1A00001",
                            .product = "Stream Deck Plus",
                            .unit_info = {0x02, 0x04, 0x00, 0x78, 0x00, 0x78, 0x00, 0x20, 0x03, 0xe0, 0x01,
                                          0x04, 0x05, 0x20, 0x00, 0x04, 0x00, 0x20, 0x03, 0x64, 0x00}};
    dock.device = &dev;
    uint8_t out[DB_CORA_REPORT_LEN];
    db_cora_dock_report(0x1c, &dock, out);
    assert_prefix("010b7c0002020400780078002003e00104052000040020036400d90f8400456c6761746f000000000000000000"
                  "000000000000000000000000000000000053747265616d204465636b20506c7573000000000000000000000000"
                  "00000000454c30304b314130303030310000000000000000000000000000000000000000",
                  out); /* a Stream Deck +, up to the port */
    TEST_ASSERT_EQUAL_UINT16(5344, db_get_u16(out + 126));
    dock.device_port = 5346; /* the second deck's device */
    db_cora_dock_report(0x1c, &dock, out);
    TEST_ASSERT_EQUAL_UINT16(5346, db_get_u16(out + 126));
}

static void test_model_names_match_the_dock(void)
{
    TEST_ASSERT_EQUAL_STRING("Stream Deck + 20GBD9901", db_cora_model(0x0084));
    TEST_ASSERT_EQUAL_STRING("Stream Deck Mini 20GAI9901", db_cora_model(0x00b8));
    TEST_ASSERT_EQUAL_STRING("Stream Deck XL 20GAT9902", db_cora_model(0x00ba));
    TEST_ASSERT_EQUAL_STRING("Stream Deck Series", db_cora_model(0x00aa));
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_header_round_trip);
    RUN_TEST(test_reports_match_protocol);
    RUN_TEST(test_attached_device_info);
    RUN_TEST(test_model_names_match_the_dock);
    return UNITY_END();
}
