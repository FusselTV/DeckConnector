#include <stdlib.h>
#include <string.h>

#include "db_stream.h"
#include "unity.h"

void setUp(void)
{
}

void tearDown(void)
{
}

#define LEN 40001 /* odd: the last byte is half a sample */

static char src[LEN];
static int16_t heard[LEN / 2];

typedef struct {
    size_t sent, heard, sent_at_first_play;
    unsigned seed;
    int break_at, stop_after;
    int plays, silent;
    size_t end;                 /* sends DB_STREAM_END after this many bytes; 0: never */
    size_t stall_at, stall_for; /* nothing for this many calls once stall_at bytes went */
    size_t stall_again;         /* then the same stall again this many bytes later; 0: once */
    size_t stalled;
} fake_t;

/* Pieces of any size, and now and then nothing when the caller does not wait: like a socket. */
static int fake_recv(void *ctx, char *buf, size_t len, bool wait)
{
    fake_t *f = ctx;
    if (f->break_at >= 0 && f->sent >= (size_t)f->break_at) {
        return -1;
    }
    if (f->end && f->sent >= f->end) {
        return DB_STREAM_END;
    }
    if (f->stalled < f->stall_for && f->sent >= f->stall_at) {
        if (++f->stalled == f->stall_for && f->stall_again) {
            f->stalled = 0;
            f->stall_at += f->stall_again;
        }
        return 0;
    }
    size_t n = (size_t)(rand_r(&f->seed) % 3000);
    if (!n && !wait) {
        return 0;
    }
    n = n ? n : 1;
    n = n < len ? n : len;
    size_t left = (f->end ? f->end : LEN) - f->sent;
    if (!left) {
        return 0;
    }
    n = n < left ? n : left;
    memcpy(buf, src + f->sent, n);
    f->sent += n;
    return (int)n;
}

/* Silence is a run of zero samples: the source has none. */
static bool fake_play(void *ctx, int16_t *pcm, size_t n)
{
    fake_t *f = ctx;
    if (!pcm[0]) {
        f->silent++;
        return f->stop_after < 0 || ++f->plays < f->stop_after;
    }
    if (f->heard == 0) {
        f->sent_at_first_play = f->sent;
    }
    memcpy(heard + f->heard, pcm, n * 2);
    f->heard += n;
    return f->stop_after < 0 || ++f->plays < f->stop_after;
}

static void run(size_t cap, unsigned seed)
{
    static int16_t ring[20000];
    fake_t f = {.seed = seed, .break_at = -1, .stop_after = -1};
    db_stream_io_t io = {fake_recv, fake_play, &f};
    memset(heard, 0, sizeof(heard));
    TEST_ASSERT_TRUE(db_stream_run(&io, ring, cap, LEN));
    TEST_ASSERT_EQUAL_size_t(LEN, f.sent);
    TEST_ASSERT_EQUAL_size_t(LEN / 2, f.heard);
    TEST_ASSERT_EQUAL_MEMORY(src, heard, LEN - 1);
    size_t ring_bytes = cap * 2 - (cap < 1024 ? cap : 1024);        /* the end of it holds silence */
    size_t start = ring_bytes / 2 < 16000 ? ring_bytes / 2 : 16000; /* half a second, or half the ring */
    TEST_ASSERT_GREATER_OR_EQUAL_size_t(start, f.sent_at_first_play);
}

static void test_every_sample_comes_out_in_order_through_any_ring(void)
{
    for (unsigned i = 0; i < LEN; i++) {
        src[i] = (char)(rand() | 1);
    }
    const size_t caps[] = {600, 1001 * 2, 8000, 20000};
    for (size_t c = 0; c < sizeof(caps) / sizeof(caps[0]); c++) {
        for (unsigned seed = 1; seed < 20; seed++) {
            run(caps[c], seed);
        }
    }
}

static void test_a_broken_stream_or_a_refused_play_ends_it(void)
{
    static int16_t ring[4000];
    fake_t f = {.seed = 7, .break_at = 20000, .stop_after = -1};
    db_stream_io_t io = {fake_recv, fake_play, &f};
    TEST_ASSERT_FALSE(db_stream_run(&io, ring, 4000, LEN));
    f = (fake_t){.seed = 7, .break_at = -1, .stop_after = 3};
    TEST_ASSERT_FALSE(db_stream_run(&io, ring, 4000, LEN));
    TEST_ASSERT_EQUAL_INT(3, f.plays);
}

static void test_a_stream_of_unknown_length_ends_when_the_sender_finishes(void)
{
    static int16_t ring[8000];
    fake_t f = {.seed = 3, .break_at = -1, .stop_after = -1, .end = LEN - 1};
    db_stream_io_t io = {fake_recv, fake_play, &f};
    memset(heard, 0, sizeof(heard));
    TEST_ASSERT_TRUE(db_stream_run(&io, ring, 8000, SIZE_MAX));
    TEST_ASSERT_EQUAL_size_t(LEN / 2, f.heard);
    TEST_ASSERT_EQUAL_MEMORY(src, heard, LEN - 1);
}

static void test_a_stall_plays_silence_and_then_the_rest(void)
{
    static int16_t ring[8000];
    /* 80 calls without data: 80 silent chunks of 32 ms, 2.6 s, while the ring runs dry */
    fake_t f = {
        .seed = 5, .break_at = -1, .stop_after = -1, .end = LEN - 1, .stall_at = 20000, .stall_for = 80};
    db_stream_io_t io = {fake_recv, fake_play, &f};
    memset(heard, 0, sizeof(heard));
    TEST_ASSERT_TRUE(db_stream_run(&io, ring, 8000, SIZE_MAX));
    TEST_ASSERT_EQUAL_size_t(LEN / 2, f.heard);
    TEST_ASSERT_EQUAL_MEMORY(src, heard, LEN - 1);
    TEST_ASSERT_GREATER_OR_EQUAL_INT(40, f.silent);
}

/* The 5 s count starts again with each piece of data: three stalls of 3 s each are not one of 9 s. */
static void test_stalls_do_not_add_up(void)
{
    static int16_t ring[8000];
    fake_t f = {.seed = 9,
                .break_at = -1,
                .stop_after = -1,
                .end = LEN - 1,
                .stall_at = 8000,
                .stall_for = 94,
                .stall_again = 10000};
    db_stream_io_t io = {fake_recv, fake_play, &f};
    memset(heard, 0, sizeof(heard));
    TEST_ASSERT_TRUE(db_stream_run(&io, ring, 8000, SIZE_MAX));
    TEST_ASSERT_EQUAL_size_t(LEN / 2, f.heard);
    TEST_ASSERT_GREATER_OR_EQUAL_INT(3 * 94, f.silent);
}

static void test_five_seconds_without_data_end_it(void)
{
    static int16_t ring[8000];
    fake_t f = {.seed = 5, .break_at = -1, .stop_after = -1, .stall_at = 20000, .stall_for = 100000};
    db_stream_io_t io = {fake_recv, fake_play, &f};
    TEST_ASSERT_FALSE(db_stream_run(&io, ring, 8000, SIZE_MAX));
    TEST_ASSERT_GREATER_OR_EQUAL_INT(160000 / 1024, f.silent); /* 32 ms each */
    TEST_ASSERT_GREATER_THAN_size_t(20000 / 2 - 512, f.heard); /* less than a chunk stays unplayed */
}

static void test_nothing_to_play_ends_at_once(void)
{
    static int16_t ring[100];
    fake_t f = {.seed = 1, .break_at = -1, .stop_after = -1};
    db_stream_io_t io = {fake_recv, fake_play, &f};
    TEST_ASSERT_TRUE(db_stream_run(&io, ring, 100, 0));
    TEST_ASSERT_TRUE(db_stream_run(&io, ring, 100, 1));
    TEST_ASSERT_EQUAL_INT(0, f.plays);
}

static void test_gain_follows_the_tone_curve(void)
{
    int16_t s[3] = {32767, -32768, 1000};
    db_stream_gain(s, 3, 50);
    TEST_ASSERT_EQUAL_INT16(8191, s[0]);
    TEST_ASSERT_EQUAL_INT16(-8192, s[1]);
    TEST_ASSERT_EQUAL_INT16(250, s[2]);
    db_stream_gain(s, 3, 0);
    TEST_ASSERT_EQUAL_INT16(0, s[0]);
    int16_t t = 1234;
    db_stream_gain(&t, 1, 150);
    TEST_ASSERT_EQUAL_INT16(1234, t);
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_every_sample_comes_out_in_order_through_any_ring);
    RUN_TEST(test_a_broken_stream_or_a_refused_play_ends_it);
    RUN_TEST(test_a_stream_of_unknown_length_ends_when_the_sender_finishes);
    RUN_TEST(test_a_stall_plays_silence_and_then_the_rest);
    RUN_TEST(test_stalls_do_not_add_up);
    RUN_TEST(test_five_seconds_without_data_end_it);
    RUN_TEST(test_nothing_to_play_ends_at_once);
    RUN_TEST(test_gain_follows_the_tone_curve);
    return UNITY_END();
}
