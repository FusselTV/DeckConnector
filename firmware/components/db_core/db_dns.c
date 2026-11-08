#include "db_dns.h"

#include <stdbool.h>
#include <stdio.h>
#include <string.h>

size_t db_dns_ptr_query(uint32_t ip, uint16_t id, uint8_t *out)
{
    const uint8_t *a = (const uint8_t *)&ip;
    uint8_t head[12] = {
        (uint8_t)(id >> 8), (uint8_t)id, 0x01, 0x00, 0, 1}; /* recursion desired, 1 question */
    memcpy(out, head, sizeof(head));
    size_t n = sizeof(head);
    for (int i = 3; i >= 0; i--) {
        int len = snprintf((char *)out + n + 1, 4, "%u", a[i]);
        out[n] = (uint8_t)len;
        n += (size_t)len + 1;
    }
    static const uint8_t tail[] = {7, 'i', 'n', '-', 'a', 'd', 'd', 'r', 4, 'a', 'r', 'p', 'a', 0, 0, 0x0c, 0, 1};
    memcpy(out + n, tail, sizeof(tail)); /* type PTR, class IN */
    return n + sizeof(tail);
}

/* Past the name at `at`; 0 when it runs off the end. */
static size_t skip_name(const uint8_t *msg, size_t len, size_t at)
{
    while (at < len) {
        if (msg[at] >= 0xc0) {
            return at + 2 <= len ? at + 2 : 0;
        }
        if (msg[at] == 0) {
            return at + 1;
        }
        at += msg[at] + 1U;
    }
    return 0;
}

bool db_dns_ptr_name(const uint8_t *msg, size_t len, uint16_t id, char name[DB_DNS_NAME_LEN])
{
    if (len < 12 || msg[0] != (uint8_t)(id >> 8) || msg[1] != (uint8_t)id || !(msg[2] & 0x80) ||
        (msg[3] & 0x0f) || msg[7] == 0) {
        return false;
    }
    size_t at = 12;
    for (int q = msg[5]; q > 0 && at; q--) {
        at = skip_name(msg, len, at);
        at = at ? at + 4 : 0;
    }
    for (int answers = msg[7]; answers > 0 && at && at < len; answers--) {
        at = skip_name(msg, len, at);
        if (!at || at + 10 > len) {
            return false;
        }
        uint16_t type = (uint16_t)(msg[at] << 8 | msg[at + 1]);
        size_t rdlen = (size_t)(msg[at + 8] << 8 | msg[at + 9]);
        at += 10;
        if (at + rdlen > len) {
            return false;
        }
        if (type == 12) {
            size_t p = at;
            for (int hops = 0; p + 1 < len && msg[p] >= 0xc0 && hops < 8; hops++) { /* a compressed name */
                p = (size_t)((msg[p] & 0x3f) << 8 | msg[p + 1]);
            }
            size_t label = p < len ? msg[p] : 0;
            if (label == 0 || label >= 0xc0 || p + 1 + label > len) {
                return false;
            }
            snprintf(name, DB_DNS_NAME_LEN, "%.*s", (int)label, (const char *)msg + p + 1);
            return true;
        }
        at += rdlen;
    }
    return false;
}
