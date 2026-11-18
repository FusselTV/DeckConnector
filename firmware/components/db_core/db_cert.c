#include "db_cert.h"

#include <stdio.h>
#include <string.h>
#include <strings.h>

#define RENEW_S (30LL * 86400)

/* The device CA may only vouch for these (its name constraints): an address outside would void the whole
 * certificate. */
static bool private_ip(uint32_t ip)
{
    const uint8_t *b = (const uint8_t *)&ip;
    return b[0] == 10 || (b[0] == 172 && (b[1] & 0xf0) == 16) || (b[0] == 192 && b[1] == 168) ||
           (b[0] == 169 && b[1] == 254);
}

/* `list` has `item` as one of its comma separated entries. */
static bool listed(const char *list, const char *item, size_t len)
{
    for (const char *p = list; *p; p += strcspn(p, ",") + (p[strcspn(p, ",")] == ',')) {
        if (strcspn(p, ",") == len && strncasecmp(p, item, len) == 0) {
            return true;
        }
    }
    return false;
}

bool db_cert_names(const char *name, const uint32_t *ips, int n, char *out, size_t cap)
{
    int len = snprintf(out, cap, "%s.local,localhost", name);
    for (int i = 0; i < n && len >= 0 && (size_t)len < cap; i++) {
        const uint8_t *b = (const uint8_t *)&ips[i];
        char ip[16];
        int l = snprintf(ip, sizeof(ip), "%u.%u.%u.%u", b[0], b[1], b[2], b[3]);
        if (ips[i] && private_ip(ips[i]) && !listed(out, ip, (size_t)l)) {
            len += snprintf(out + len, cap - (size_t)len, ",%s", ip);
        }
    }
    return len >= 0 && (size_t)len < cap;
}

bool db_cert_renew(const char *have, const char *want, int64_t now, int64_t until)
{
    size_t first = strcspn(want, ",");
    if (!have[0] || strcspn(have, ",") != first || strncasecmp(have, want, first) != 0 ||
        until - now < RENEW_S) {
        return true;
    }
    for (const char *p = want; *p; p += strcspn(p, ",") + (p[strcspn(p, ",")] == ',')) {
        if (!listed(have, p, strcspn(p, ","))) {
            return true;
        }
    }
    return false;
}
