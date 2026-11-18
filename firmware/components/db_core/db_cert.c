#include "db_cert.h"

#include <stdio.h>
#include <string.h>
#include <strings.h>

#define RENEW_S (30LL * 86400)

bool db_cert_names(const char *name, char *out, size_t cap)
{
    int len = snprintf(out, cap, "%s.local,localhost", name);
    return len >= 0 && (size_t)len < cap;
}

/* NameConstraints { permittedSubtrees [0] { dNSName [2] <name>.local, localhost },
 *                   excludedSubtrees [1] { iPAddress [7] 0.0.0.0/0, ::/0 } }: a permitted list without an
 * address leaves every address allowed (RFC 5280 checks each kind of name on its own). All lengths stay below
 * 128. */
size_t db_cert_constraints(const char *name, uint8_t *out, size_t cap)
{
    char dns[2][64];
    int l0 = snprintf(dns[0], sizeof(dns[0]), "%s.local", name);
    int l1 = snprintf(dns[1], sizeof(dns[1]), "localhost");
    size_t permitted = 8 + (size_t)l0 + (size_t)l1;
    size_t excluded = 4 + 8 + 4 + 32;
    size_t total = 2 + permitted + 2 + excluded;
    if (l0 < 0 || (size_t)l0 >= sizeof(dns[0]) || total + 2 > cap || total > 127) {
        return 0;
    }
    uint8_t *p = out;
    *p++ = 0x30;
    *p++ = (uint8_t)total;
    *p++ = 0xa0;
    *p++ = (uint8_t)permitted;
    for (int i = 0; i < 2; i++) {
        size_t l = (size_t)(i ? l1 : l0);
        *p++ = 0x30;
        *p++ = (uint8_t)(l + 2);
        *p++ = 0x82;
        *p++ = (uint8_t)l;
        memcpy(p, dns[i], l);
        p += l;
    }
    *p++ = 0xa1;
    *p++ = (uint8_t)excluded;
    for (int len = 8; len <= 32; len += 24) { /* address and mask, all zero: every IPv4, then every IPv6 */
        *p++ = 0x30;
        *p++ = (uint8_t)(len + 2);
        *p++ = 0x87;
        *p++ = (uint8_t)len;
        memset(p, 0, (size_t)len);
        p += len;
    }
    return (size_t)(p - out);
}

bool db_cert_renew(const char *have, const char *want, int64_t now, int64_t until)
{
    return strcasecmp(have, want) != 0 || until - now < RENEW_S;
}
