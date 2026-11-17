#include "db_version.h"

#include <ctype.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
    bool ok;
    long num[3];
    bool pre;
    char pre_name[16]; /* "rc" of rc4 */
    long pre_num;
    long after; /* commits after the tag */
} ver_t;

static long number(const char **p)
{
    char *end;
    long n = strtol(*p, &end, 10);
    bool any = end != *p;
    *p = end;
    return any ? n : -1;
}

static ver_t parse(const char *s)
{
    ver_t v = {0};
    if (!s || (*s != 'v' && *s != 'V')) {
        return v;
    }
    s++;
    for (int i = 0; i < 3; i++) {
        if (!isdigit((unsigned char)*s)) {
            return v;
        }
        v.num[i] = number(&s);
        if (i < 2 && *s != '.') {
            return v;
        }
        s += i < 2;
    }
    v.ok = true;
    /* "-rc4", then "-7-gabc1234", then "-dirty" */
    if (*s == '-' && isalpha((unsigned char)s[1]) && !(s[1] == 'g' && isxdigit((unsigned char)s[2])) &&
        strncmp(s + 1, "dirty", 5) != 0) {
        s++;
        size_t n = 0;
        while (isalpha((unsigned char)*s) && n < sizeof(v.pre_name) - 1) {
            v.pre_name[n++] = (char)tolower((unsigned char)*s++);
        }
        v.pre = true;
        v.pre_num = isdigit((unsigned char)*s) ? number(&s) : 0;
    }
    if (*s == '-' && isdigit((unsigned char)s[1])) {
        s++;
        long after = number(&s);
        v.after = strncmp(s, "-g", 2) == 0 ? after : 0;
    }
    return v;
}

int db_version_cmp(const char *a, const char *b)
{
    ver_t x = parse(a);
    ver_t y = parse(b);
    if (x.ok != y.ok) {
        return x.ok ? 1 : -1;
    }
    if (!x.ok) {
        return 0;
    }
    for (int i = 0; i < 3; i++) {
        if (x.num[i] != y.num[i]) {
            return x.num[i] < y.num[i] ? -1 : 1;
        }
    }
    if (x.pre != y.pre) {
        return x.pre ? -1 : 1; /* v1.0.0-rc4 comes before v1.0.0 */
    }
    if (x.pre) {
        int c = strcmp(x.pre_name, y.pre_name);
        if (c) {
            return c < 0 ? -1 : 1;
        }
        if (x.pre_num != y.pre_num) {
            return x.pre_num < y.pre_num ? -1 : 1;
        }
    }
    return x.after == y.after ? 0 : x.after < y.after ? -1 : 1;
}

bool db_version_pre(const char *v)
{
    return parse(v).pre;
}
