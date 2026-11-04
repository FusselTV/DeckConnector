#pragma once

#include <stddef.h>

#include "esp_err.h"

/*
 * Web UI and JSON API on port 80, behind HTTP basic auth (user admin, the generated password). While the
 * setup access point is on, a DNS server sends every name to the bridge so phones open the page by
 * themselves.
 */

/* `status_json` writes the body of /api/status. */
esp_err_t db_web_start(size_t (*status_json)(char *buf, size_t cap));
