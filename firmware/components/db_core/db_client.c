#include "db_client.h"

#include <string.h>

bool db_ipv4(const char *s, size_t len, uint32_t *out)
{
    uint8_t b[4];
    size_t i = 0;
    for (int part = 0; part < 4; part++) {
        size_t start = i;
        unsigned v = 0;
        while (i < len && s[i] >= '0' && s[i] <= '9' && i - start < 3) {
            v = v * 10 + (unsigned)(s[i++] - '0');
        }
        size_t digits = i - start;
        if (!digits || v > 255 || (digits > 1 && s[start] == '0')) {
            return false;
        }
        b[part] = (uint8_t)v;
        if (part < 3) {
            if (i >= len || s[i] != '.') {
                return false;
            }
            i++;
        }
    }
    if (i != len) {
        return false;
    }
    memcpy(out, b, 4);
    return true;
}

uint32_t db_client_ip(uint32_t peer, uint32_t proxy, const char *forwarded_for)
{
    if (!proxy || peer != proxy || !forwarded_for) {
        return peer;
    }
    const char *last = strrchr(forwarded_for, ',');
    last = last ? last + 1 : forwarded_for;
    last += strspn(last, " \t");
    size_t len = strlen(last);
    while (len && (last[len - 1] == ' ' || last[len - 1] == '\t')) {
        len--;
    }
    uint32_t ip;
    return db_ipv4(last, len, &ip) && ip ? ip : peer;
}
