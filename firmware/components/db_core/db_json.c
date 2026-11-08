#include "db_json.h"

#include <inttypes.h>
#include <stdio.h>
#include <string.h>

void db_json_begin(db_json_t *j, char *buf, size_t cap)
{
    *j = (db_json_t){.buf = buf, .cap = cap};
    if (cap > 0) {
        buf[0] = '\0';
    } else {
        j->overflow = true;
    }
}

size_t db_json_end(db_json_t *j)
{
    return j->overflow ? 0 : j->len;
}

static void put(db_json_t *j, const char *s, size_t n)
{
    if (j->overflow || j->len + n + 1 > j->cap) {
        j->overflow = true;
        return;
    }
    memcpy(j->buf + j->len, s, n);
    j->len += n;
    j->buf[j->len] = '\0';
}

static void puts_raw(db_json_t *j, const char *s)
{
    put(j, s, strlen(s));
}

static void put_escaped(db_json_t *j, const char *s)
{
    put(j, "\"", 1);
    for (; *s; s++) {
        unsigned char c = (unsigned char)*s;
        if (c == '"' || c == '\\') {
            char esc[2] = {'\\', (char)c};
            put(j, esc, 2);
        } else if (c < 0x20) {
            char esc[8];
            int n = snprintf(esc, sizeof(esc), "\\u%04x", c);
            put(j, esc, (size_t)n);
        } else {
            put(j, (const char *)&c, 1);
        }
    }
    put(j, "\"", 1);
}

static void key(db_json_t *j, const char *k)
{
    if (j->need_comma) {
        put(j, ",", 1);
    }
    if (k) {
        put_escaped(j, k);
        put(j, ":", 1);
    }
    j->need_comma = true;
}

void db_json_obj_open(db_json_t *j, const char *k)
{
    key(j, k);
    put(j, "{", 1);
    j->need_comma = false;
}

void db_json_obj_close(db_json_t *j)
{
    put(j, "}", 1);
    j->need_comma = true;
}

void db_json_arr_open(db_json_t *j, const char *k)
{
    key(j, k);
    put(j, "[", 1);
    j->need_comma = false;
}

void db_json_arr_close(db_json_t *j)
{
    put(j, "]", 1);
    j->need_comma = true;
}

void db_json_str(db_json_t *j, const char *k, const char *value)
{
    key(j, k);
    put_escaped(j, value ? value : "");
}

void db_json_int(db_json_t *j, const char *k, int64_t value)
{
    char num[24];
    int n = snprintf(num, sizeof(num), "%" PRId64, value);
    key(j, k);
    put(j, num, (size_t)n);
}

void db_json_bool(db_json_t *j, const char *k, bool value)
{
    key(j, k);
    puts_raw(j, value ? "true" : "false");
}

void db_json_null(db_json_t *j, const char *k)
{
    key(j, k);
    puts_raw(j, "null");
}

void db_hex(char *out, const uint8_t *in, size_t n)
{
    static const char digits[] = "0123456789abcdef";
    for (size_t i = 0; i < n; i++) {
        out[2 * i] = digits[in[i] >> 4];
        out[2 * i + 1] = digits[in[i] & 15];
    }
    out[2 * n] = '\0';
}
