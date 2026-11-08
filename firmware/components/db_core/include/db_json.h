#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/*
 * Minimal JSON writer into a fixed buffer. Commas are inserted automatically. Once the buffer is full every
 * further call is a no-op and db_json_end() returns 0.
 */
typedef struct {
    char *buf;
    size_t cap;
    size_t len;
    bool overflow;
    bool need_comma;
} db_json_t;

void db_json_begin(db_json_t *j, char *buf, size_t cap);
size_t db_json_end(db_json_t *j);

void db_json_obj_open(db_json_t *j, const char *key);
void db_json_obj_close(db_json_t *j);
void db_json_arr_open(db_json_t *j, const char *key);
void db_json_arr_close(db_json_t *j);

/* `key` is NULL inside arrays. */
void db_json_str(db_json_t *j, const char *key, const char *value);
void db_json_int(db_json_t *j, const char *key, int64_t value);
void db_json_bool(db_json_t *j, const char *key, bool value);
void db_json_null(db_json_t *j, const char *key);

/* Lowercase hex of `n` bytes, NUL-terminated: `out` holds 2n + 1. */
void db_hex(char *out, const uint8_t *in, size_t n);
