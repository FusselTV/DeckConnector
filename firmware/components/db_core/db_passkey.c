#include "db_passkey.h"

#include <string.h>

bool db_authdata(const uint8_t *ad, size_t len, db_authdata_t *out)
{
    if (len < 37) {
        return false;
    }
    *out = (db_authdata_t){.rp_hash = ad, .flags = ad[32]};
    out->count = (uint32_t)ad[33] << 24 | (uint32_t)ad[34] << 16 | (uint32_t)ad[35] << 8 | ad[36];
    if (!(out->flags & DB_AD_AT)) {
        return true;
    }
    if (len < 37 + 16 + 2) {
        return false;
    }
    out->cred_len = (size_t)ad[53] << 8 | ad[54];
    out->cred = ad + 55;
    return out->cred_len && 55 + out->cred_len <= len;
}

bool db_spki_p256(const uint8_t *der, size_t len, uint8_t xy[64])
{
    static const uint8_t head[] = {0x30, 0x59, 0x30, 0x13, 0x06, 0x07, 0x2a, 0x86, 0x48,
                                   0xce, 0x3d, 0x02, 0x01, 0x06, 0x08, 0x2a, 0x86, 0x48,
                                   0xce, 0x3d, 0x03, 0x01, 0x07, 0x03, 0x42, 0x00, 0x04};
    if (len != sizeof(head) + 64 || memcmp(der, head, sizeof(head)) != 0) {
        return false;
    }
    memcpy(xy, der + sizeof(head), 64);
    return true;
}

bool db_count_ok(uint32_t stored, uint32_t got)
{
    return got > stored || (got == 0 && stored == 0);
}

static const char b64url_digits[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-_";

int db_b64url(const char *in, uint8_t *out, size_t cap)
{
    uint32_t acc = 0;
    int bits = 0;
    size_t n = 0;
    for (const char *c = in; *c && *c != '='; c++) {
        const char *at = strchr(b64url_digits, *c);
        if (!at) {
            return -1;
        }
        acc = acc << 6 | (uint32_t)(at - b64url_digits);
        bits += 6;
        if (bits >= 8) {
            bits -= 8;
            if (n == cap) {
                return -1;
            }
            out[n++] = (uint8_t)(acc >> bits);
        }
    }
    return bits < 6 ? (int)n : -1;
}
