#include "db_improv.h"

#include <string.h>

static const uint8_t header[6] = {'I', 'M', 'P', 'R', 'O', 'V'};

static uint8_t checksum(const uint8_t *b, size_t n)
{
    unsigned sum = 0;
    for (size_t i = 0; i < n; i++) {
        sum += b[i];
    }
    return (uint8_t)sum;
}

db_improv_feed_t db_improv_feed(db_improv_parser_t *p, uint8_t byte, db_improv_packet_t *out)
{
    if (p->got < sizeof(header)) {
        /* 'I' is only at the start of the header, so a mismatch can only restart on this byte */
        p->got = byte == header[p->got] ? p->got + 1 : byte == header[0];
        memcpy(p->buf, header, sizeof(header));
        return DB_IMPROV_MORE;
    }
    if (p->got == 6 && byte != 1) { /* version */
        p->got = byte == header[0];
        return DB_IMPROV_MORE;
    }
    if (p->got < 9 || p->got < 9U + p->buf[8]) {
        p->buf[p->got++] = byte;
        return DB_IMPROV_MORE;
    }
    size_t n = p->got;
    p->got = 0;
    if (checksum(p->buf, n) != byte) {
        return DB_IMPROV_BAD_CHECKSUM;
    }
    *out = (db_improv_packet_t){.type = p->buf[7], .len = p->buf[8], .data = p->buf + 9};
    return DB_IMPROV_PACKET;
}

size_t db_improv_frame(uint8_t *out, uint8_t type, const uint8_t *data, uint8_t len)
{
    out[0] = '\n';
    memcpy(out + 1, header, sizeof(header));
    out[7] = 1;
    out[8] = type;
    out[9] = len;
    memcpy(out + 10, data, len);
    out[10 + len] = checksum(out + 1, 9U + len);
    out[11 + len] = '\n';
    return 12U + len;
}

bool db_improv_rpc(const db_improv_packet_t *p, uint8_t *cmd, const uint8_t **data, uint8_t *len)
{
    if (p->type != DB_IMPROV_RPC || p->len < 2 || p->data[1] != p->len - 2) {
        return false;
    }
    *cmd = p->data[0];
    *len = p->data[1];
    *data = p->data + 2;
    return true;
}

bool db_improv_str(const uint8_t *data, uint8_t len, size_t *pos, char *out, size_t cap)
{
    if (*pos >= len) {
        return false;
    }
    size_t n = data[*pos];
    const uint8_t *s = data + *pos + 1;
    if (*pos + 1 + n > len || n >= cap || memchr(s, 0, n)) {
        return false;
    }
    memcpy(out, s, n);
    out[n] = '\0';
    *pos += 1 + n;
    return true;
}

size_t db_improv_result(uint8_t *out, uint8_t cmd, const char *const *strs, size_t n)
{
    size_t at = 2;
    for (size_t i = 0; i < n; i++) {
        size_t l = strlen(strs[i]);
        if (l > 255 || at + 1 + l > 255) {
            return 0;
        }
        out[at] = (uint8_t)l;
        memcpy(out + at + 1, strs[i], l);
        at += 1 + l;
    }
    out[0] = cmd;
    out[1] = (uint8_t)(at - 2);
    return at;
}
