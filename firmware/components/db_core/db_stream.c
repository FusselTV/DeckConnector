#include "db_stream.h"

#define START ((size_t)16000) /* bytes: half a second at 16 kHz, 2 bytes a sample */
#define CHUNK ((size_t)1024)  /* bytes played at a time, 512 samples, 32 ms */

static size_t least(size_t a, size_t b)
{
    return a < b ? a : b;
}

bool db_stream_run(const db_stream_io_t *io, int16_t *ring, size_t cap, size_t len)
{
    char *buf = (char *)ring;
    size_t size = cap * 2;
    size_t start = least(START, size / 2);
    size_t in = 0;  /* bytes received so far */
    size_t out = 0; /* bytes played so far */
    bool playing = false;
    for (;;) {
        size_t held = in - out;
        if (in < len && held < size) {
            bool wait = held < (playing ? CHUNK : start);
            size_t at = in % size;
            int got = io->recv(io->ctx, buf + at, least(least(size - held, size - at), len - in), wait);
            if (got < 0) {
                return false;
            }
            in += (size_t)got;
            if (wait) {
                continue;
            }
        }
        held = (in - out) & ~(size_t)1; /* an odd last byte is half a sample */
        if (!held) {
            return true; /* only here once all came: else it waited above */
        }
        playing = true;
        size_t at = out % size;
        size_t n = least(least(held, CHUNK), size - at);
        if (!io->play(io->ctx, (int16_t *)(void *)(buf + at), n / 2)) {
            return false;
        }
        out += n;
    }
}

void db_stream_gain(int16_t *pcm, size_t n, int volume)
{
    volume = volume < 0 ? 0 : volume > 100 ? 100 : volume;
    int32_t g = volume * volume;
    for (size_t i = 0; i < n; i++) {
        pcm[i] = (int16_t)(pcm[i] * g / 10000);
    }
}
