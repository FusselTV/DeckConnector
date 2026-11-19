#include "db_version.h"

#include <ctype.h>
#include <stdio.h>
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

#define TAG_PATH "/releases/tag/"

static const char *find(const char *from, const char *to, const char *what)
{
    size_t n = strlen(what);
    for (; from + n <= to; from++) {
        if (memcmp(from, what, n) == 0) {
            return from;
        }
    }
    return NULL;
}

bool db_version_newest(const char *feed, bool pre_ok, char *tag, size_t tag_cap, char *base, size_t base_cap)
{
    tag[0] = base[0] = '\0';
    /* notes are escaped HTML in the feed: a raw <link> is always an entry's own */
    const char *stop = feed ? feed + strlen(feed) : NULL;
    const char *end;
    for (const char *p = feed;
         p && (p = find(p, stop, "<link ")) && (end = memchr(p, '>', (size_t)(stop - p))); p = end) {
        const char *href = find(p, end, "href=\"");
        if (!href) {
            continue;
        }
        href += strlen("href=\"");
        const char *url_end = href;
        while (url_end < end && *url_end != '"' && *url_end != '<') {
            url_end++;
        }
        const char *at = find(href, url_end, TAG_PATH);
        if (!at) {
            continue;
        }
        const char *t = at + strlen(TAG_PATH);
        size_t len = (size_t)(url_end - t);
        size_t base_len = (size_t)(at - href);
        char v[64];
        if (len == 0 || len >= tag_cap || len >= sizeof(v) || base_len >= base_cap) {
            continue;
        }
        snprintf(v, sizeof(v), "%.*s", (int)len, t);
        if (db_version_cmp(v, "v0.0.0") < 0 || (!pre_ok && db_version_pre(v)) ||
            (tag[0] && db_version_cmp(v, tag) <= 0)) {
            continue;
        }
        snprintf(tag, tag_cap, "%s", v);
        snprintf(base, base_cap, "%.*s", (int)base_len, href);
    }
    return tag[0] != '\0';
}
