#include "db_stream.h"

#include <string.h>

#define START ((size_t)16000)  /* bytes: half a second at 16 kHz, 2 bytes a sample */
#define CHUNK ((size_t)1024)   /* bytes played at a time, 512 samples, 32 ms */
#define QUIET ((size_t)160000) /* bytes of silence in a row: 5 s without data end the stream */

static size_t least(size_t a, size_t b)
{
    return a < b ? a : b;
}

bool db_stream_run(const db_stream_io_t *io, int16_t *ring, size_t cap, size_t len)
{
    char *buf = (char *)ring;
    size_t hush = least(CHUNK, cap) & ~(size_t)1; /* the ring's last bytes stay silence */
    size_t size = cap * 2 - hush;
    memset(buf + size, 0, hush);
    size_t start = least(START, size / 2);
    size_t in = 0;    /* bytes received so far */
    size_t out = 0;   /* bytes played so far */
    size_t quiet = 0; /* bytes of silence played since the last data */
    bool ended = !len, buffering = true;
    for (bool first = true;; first = false) {
        size_t held = in - out;
        if (!ended && held < size) {
            size_t at = in % size;
            /* waits once: the first bytes may sit in the server's own buffer, which a poll does not see */
            int got = io->recv(io->ctx, buf + at, least(least(size - held, size - at), len - in), first);
            if (got == DB_STREAM_END) {
                ended = true;
            } else if (got < 0) {
                return false;
            } else {
                in += (size_t)got;
                quiet = got ? 0 : quiet;
                ended = in == len;
            }
        }
        held = (in - out) & ~(size_t)1; /* an odd last byte is half a sample */
        if (!held && ended) {
            return true;
        }
        buffering = !ended && held < (buffering ? start : CHUNK);
        if (buffering) {
            quiet += hush;
            if (quiet >= QUIET || !io->play(io->ctx, (int16_t *)(void *)(buf + size), hush / 2)) {
                return false;
            }
            continue;
        }
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
