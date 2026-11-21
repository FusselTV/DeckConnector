#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define DB_STREAM_END (-2)

typedef struct {
    /* Up to `len` bytes of the stream into `buf`: how many; 0 when none came (at once, or in a while when
     * `wait`); DB_STREAM_END when the sender finished; another negative when the stream broke. */
    int (*recv)(void *ctx, char *buf, size_t len, bool wait);
    /* Plays `n` samples, blocking about as long as they last; false ends the stream. */
    bool (*play)(void *ctx, int16_t *pcm, size_t n);
    void *ctx;
} db_stream_io_t;

/* Up to `len` bytes of 16-bit mono PCM from io->recv to io->play through `ring` (`cap` samples): starts once
 * half a second (at most half the ring) is in, then reads ahead while it plays. Plays silence while it waits
 * for data, also when the data stops coming for a while, and starts again after the next half second; 5 s
 * without data end it. True when all of it played, up to `len` or DB_STREAM_END. */
bool db_stream_run(const db_stream_io_t *io, int16_t *ring, size_t cap, size_t len);

/* `volume` 0 to 100 on the same curve as the tones. */
void db_stream_gain(int16_t *pcm, size_t n, int volume);
