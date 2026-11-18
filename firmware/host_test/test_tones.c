#include <stdlib.h>
#include <string.h>

#include "db_tones.h"
#include "unity.h"

void setUp(void)
{
}

void tearDown(void)
{
}

#define RATE 16000

/* The pitch of the first stretch of sound, by zero crossings. */
static int first_hz(const int16_t *s, size_t n)
{
    size_t from = 0;
    while (from < n && !s[from]) {
        from++;
    }
    size_t to = from;
    while (to < n && (s[to] || s[to + 1] || s[to + 2])) { /* to the next silence */
        to++;
    }
    int crossings = 0;
    for (size_t i = from + 1; i < to; i++) {
        crossings += (s[i - 1] < 0) != (s[i] < 0);
    }
    return to > from ? (int)((long)crossings * RATE / 2 / (long)(to - from)) : 0;
}

static void test_each_sound_has_its_pitch_and_length(void)
{
    static int16_t buf[RATE * 4];
    const int first[] = {1047, 1319, 1568, 2093};
    for (int s = 0; s < DB_SOUND_COUNT; s++) {
        size_t n = db_tones_render((db_sound_t)s, 100, RATE, buf, sizeof(buf) / sizeof(buf[0]));
        TEST_ASSERT_TRUE_MESSAGE(n > RATE / 20 && n < RATE * 3, db_sound_name((db_sound_t)s));
        int hz = first_hz(buf, n);
        TEST_ASSERT_INT_WITHIN_MESSAGE(first[s] / 20, first[s], hz, db_sound_name((db_sound_t)s));
    }
}

static void test_volume_scales_and_zero_is_silent(void)
{
    static int16_t loud[RATE], soft[RATE];
    size_t n = db_tones_render(DB_SOUND_CONNECTED, 100, RATE, loud, RATE);
    db_tones_render(DB_SOUND_CONNECTED, 50, RATE, soft, RATE);
    int16_t peak_loud = 0, peak_soft = 0;
    for (size_t i = 0; i < n; i++) {
        peak_loud = abs(loud[i]) > peak_loud ? (int16_t)abs(loud[i]) : peak_loud;
        peak_soft = abs(soft[i]) > peak_soft ? (int16_t)abs(soft[i]) : peak_soft;
    }
    TEST_ASSERT_TRUE(peak_loud > 15000 && peak_loud <= 16000);
    TEST_ASSERT_INT_WITHIN(200, peak_loud / 4, peak_soft);
    TEST_ASSERT_EQUAL_size_t(0, db_tones_render(DB_SOUND_CONNECTED, 0, RATE, loud, RATE));
    TEST_ASSERT_EQUAL_INT16(0, loud[0] / 2000); /* starts soft: no click */
}

static void test_level_finds_the_pitch_in_one_channel(void)
{
    static int16_t lr[RATE * 2], mono[RATE];
    size_t n = db_tones_render(DB_SOUND_FIND, 100, RATE, mono, RATE);
    for (size_t i = 0; i < n; i++) {
        lr[2 * i] = mono[i];
        lr[2 * i + 1] = (int16_t)(i % 7 * 100);
    }
    float tone = db_tones_level(lr, 2 * n, 2, 2093, RATE);
    TEST_ASSERT_TRUE(tone > 1000 * db_tones_level(lr, 2 * n, 2, 1500, RATE));
    TEST_ASSERT_TRUE(tone > 1000 * db_tones_level(lr + 1, 2 * n - 1, 2, 2093, RATE));
    TEST_ASSERT_EQUAL_FLOAT(0, db_tones_level(lr, 0, 2, 2093, RATE));
}

static void test_heard_needs_every_control_quiet(void)
{
    TEST_ASSERT_TRUE(db_tones_heard(1000, 50, 50, 50));
    TEST_ASSERT_FALSE(db_tones_heard(1000, 200, 50, 50));
    TEST_ASSERT_FALSE(db_tones_heard(1000, 50, 200, 50));
    TEST_ASSERT_FALSE(db_tones_heard(1000, 50, 50, 200));
    TEST_ASSERT_FALSE(db_tones_heard(0, 0, 0, 0));
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_each_sound_has_its_pitch_and_length);
    RUN_TEST(test_volume_scales_and_zero_is_silent);
    RUN_TEST(test_level_finds_the_pitch_in_one_channel);
    RUN_TEST(test_heard_needs_every_control_quiet);
    return UNITY_END();
}
