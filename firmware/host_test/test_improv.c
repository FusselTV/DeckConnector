#include <string.h>

#include "db_improv.h"
#include "unity.h"

void setUp(void)
{
}
void tearDown(void)
{
}

static db_improv_feed_t feed_all(db_improv_parser_t *p, const uint8_t *b, size_t n, db_improv_packet_t *out)
{
    db_improv_feed_t r = DB_IMPROV_MORE;
    for (size_t i = 0; i < n && r == DB_IMPROV_MORE; i++) {
        r = db_improv_feed(p, b[i], out);
    }
    return r;
}

/* The request-current-state RPC as the Improv SDK sends it. */
static const uint8_t STATE_RPC[] = {'I', 'M', 'P', 'R', 'O', 'V', 1, 3, 2, 2, 0, 0xe5};

static void test_a_packet_between_log_lines_is_found(void)
{
    uint8_t line[128];
    const char *log = "I (812) main: DeckConnector\nIMPRIMP";
    size_t n = strlen(log);
    memcpy(line, log, n);
    memcpy(line + n, STATE_RPC, sizeof(STATE_RPC));
    db_improv_parser_t p = {0};
    db_improv_packet_t pk;
    TEST_ASSERT_EQUAL(DB_IMPROV_PACKET, feed_all(&p, line, n + sizeof(STATE_RPC), &pk));
    uint8_t cmd, len;
    const uint8_t *data;
    TEST_ASSERT_TRUE(db_improv_rpc(&pk, &cmd, &data, &len));
    TEST_ASSERT_EQUAL(2, cmd);
    TEST_ASSERT_EQUAL(0, len);
}

static void test_a_bad_checksum_or_version_is_no_packet(void)
{
    uint8_t b[sizeof(STATE_RPC)];
    memcpy(b, STATE_RPC, sizeof(b));
    b[sizeof(b) - 1]++;
    db_improv_parser_t p = {0};
    db_improv_packet_t pk;
    TEST_ASSERT_EQUAL(DB_IMPROV_BAD_CHECKSUM, feed_all(&p, b, sizeof(b), &pk));
    TEST_ASSERT_EQUAL(DB_IMPROV_PACKET, feed_all(&p, STATE_RPC, sizeof(STATE_RPC), &pk));
    b[sizeof(b) - 1]--;
    b[6] = 2;
    TEST_ASSERT_EQUAL(DB_IMPROV_MORE, feed_all(&p, b, sizeof(b), &pk));
    TEST_ASSERT_EQUAL(DB_IMPROV_PACKET, feed_all(&p, STATE_RPC, sizeof(STATE_RPC), &pk));
}

static void test_a_frame_parses_back(void)
{
    uint8_t data[255];
    const char *strs[] = {"DeckConnector", "1.2.3", "ESP32-P4", "deckconnector-a1b2c3"};
    size_t n = db_improv_result(data, 3, strs, 4);
    TEST_ASSERT_EQUAL(2 + 4 + 13 + 5 + 8 + 20, n);
    uint8_t frame[DB_IMPROV_FRAME_MAX];
    size_t f = db_improv_frame(frame, DB_IMPROV_RESULT, data, (uint8_t)n);
    TEST_ASSERT_EQUAL(12 + n, f);
    TEST_ASSERT_EQUAL('\n', frame[0]);
    TEST_ASSERT_EQUAL('\n', frame[f - 1]);
    db_improv_parser_t p = {0};
    db_improv_packet_t pk;
    TEST_ASSERT_EQUAL(DB_IMPROV_PACKET, feed_all(&p, frame, f - 1, &pk));
    TEST_ASSERT_EQUAL(DB_IMPROV_RESULT, pk.type);
    TEST_ASSERT_EQUAL(3, pk.data[0]);
    size_t pos = 0;
    char s[32];
    for (int i = 0; i < 4; i++) {
        TEST_ASSERT_TRUE(db_improv_str(pk.data + 2, pk.data[1], &pos, s, sizeof(s)));
        TEST_ASSERT_EQUAL_STRING(strs[i], s);
    }
    TEST_ASSERT_FALSE(db_improv_str(pk.data + 2, pk.data[1], &pos, s, sizeof(s)));
}

static void test_the_state_frame_matches_the_spec_bytes(void)
{
    uint8_t ready = 2;
    uint8_t frame[DB_IMPROV_FRAME_MAX];
    size_t f = db_improv_frame(frame, DB_IMPROV_STATE, &ready, 1);
    const uint8_t want[] = {'\n', 'I', 'M', 'P', 'R', 'O', 'V', 1, 1, 1, 2, 0xe2, '\n'};
    TEST_ASSERT_EQUAL(sizeof(want), f);
    TEST_ASSERT_EQUAL_MEMORY(want, frame, f);
}

static void test_the_largest_packet_fits(void)
{
    uint8_t data[255];
    memset(data, 'x', sizeof(data));
    uint8_t frame[DB_IMPROV_FRAME_MAX];
    size_t f = db_improv_frame(frame, DB_IMPROV_RESULT, data, 255);
    TEST_ASSERT_EQUAL(DB_IMPROV_FRAME_MAX, f);
    db_improv_parser_t p = {0};
    db_improv_packet_t pk;
    TEST_ASSERT_EQUAL(DB_IMPROV_PACKET, feed_all(&p, frame + 1, f - 2, &pk));
    TEST_ASSERT_EQUAL(255, pk.len);
}

static void test_wifi_settings_and_bad_strings(void)
{
    /* 01 len 05 "Home!" 08 "password" */
    const uint8_t rpc[] = {1, 15, 5, 'H', 'o', 'm', 'e', '!', 8, 'p', 'a', 's', 's', 'w', 'o', 'r', 'd'};
    db_improv_packet_t pk = {.type = DB_IMPROV_RPC, .len = sizeof(rpc), .data = rpc};
    uint8_t cmd, len;
    const uint8_t *data;
    TEST_ASSERT_TRUE(db_improv_rpc(&pk, &cmd, &data, &len));
    size_t pos = 0;
    char ssid[33], pass[65];
    TEST_ASSERT_TRUE(db_improv_str(data, len, &pos, ssid, sizeof(ssid)));
    TEST_ASSERT_TRUE(db_improv_str(data, len, &pos, pass, sizeof(pass)));
    TEST_ASSERT_EQUAL_STRING("Home!", ssid);
    TEST_ASSERT_EQUAL_STRING("password", pass);

    pk.len--; /* the RPC length no longer matches */
    TEST_ASSERT_FALSE(db_improv_rpc(&pk, &cmd, &data, &len));
    pos = 0;
    TEST_ASSERT_FALSE(db_improv_str(data, 5, &pos, ssid, sizeof(ssid))); /* runs past the data */
    pos = 0;
    TEST_ASSERT_FALSE(db_improv_str(data, len, &pos, ssid, 5)); /* too long for the field */
    const uint8_t nul[] = {3, 'a', 0, 'b'};
    pos = 0;
    TEST_ASSERT_FALSE(db_improv_str(nul, sizeof(nul), &pos, ssid, sizeof(ssid)));
}

static void test_a_result_too_big_is_refused(void)
{
    char big[300];
    memset(big, 'x', sizeof(big) - 1);
    big[sizeof(big) - 1] = '\0';
    const char *strs[] = {big};
    uint8_t data[255];
    TEST_ASSERT_EQUAL(0, db_improv_result(data, 1, strs, 1));
    big[252] = '\0'; /* 2 + 1 + 252 = 255 */
    TEST_ASSERT_EQUAL(255, db_improv_result(data, 1, strs, 1));
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_a_packet_between_log_lines_is_found);
    RUN_TEST(test_a_bad_checksum_or_version_is_no_packet);
    RUN_TEST(test_a_frame_parses_back);
    RUN_TEST(test_the_state_frame_matches_the_spec_bytes);
    RUN_TEST(test_the_largest_packet_fits);
    RUN_TEST(test_wifi_settings_and_bad_strings);
    RUN_TEST(test_a_result_too_big_is_refused);
    return UNITY_END();
}
