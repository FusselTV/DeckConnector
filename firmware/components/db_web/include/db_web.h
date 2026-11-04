#pragma once

#include <stddef.h>

#include "esp_err.h"

/*
 * Web UI and JSON API on port 80, behind HTTP basic auth (user admin, the generated password). While the
 * setup access point is on, a DNS server sends every name to the bridge so phones open the page by
 * themselves.
 */

typedef struct {
    size_t (*status_json)(char *buf, size_t cap); /* full JSON object */
} db_web_hooks_t;

esp_err_t db_web_start(const db_web_hooks_t *hooks);
