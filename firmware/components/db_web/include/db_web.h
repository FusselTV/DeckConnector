#pragma once

#include <stddef.h>

#include "esp_err.h"

/*
 * Web UI and JSON API on port 80 and, with a certificate made on the device, on 443. The password (a session
 * cookie, or basic auth for scripts) or an API token opens it, see auth.h.
 */

/* Version of the API a page in the `www` partition may ask for (api_min in its .dbfw header). Raise it when a
 * route or field the page relies on changes. */
#define DB_WEB_API 1

/* `status_json` writes the body of /api/status; `state_json` the web socket's state message, which leaves out
 * what changes all the time. */
esp_err_t db_web_start(size_t (*status_json)(char *buf, size_t cap),
                       size_t (*state_json)(char *buf, size_t cap));

/* SHA-256 of the HTTPS certificate, "AB:CD:…"; empty without HTTPS. */
const char *db_web_fingerprint(void);

/* Send one JSON message to every listener of /api/ws. */
void db_web_broadcast(const char *json);
