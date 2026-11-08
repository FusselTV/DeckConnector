#pragma once

#include <stdbool.h>
#include <stddef.h>

#include "esp_err.h"

/*
 * Web UI and JSON API on port 80 and, with a certificate made on the device, on 443. The password (a session
 * cookie, or basic auth for scripts) or an API token opens it, see auth.h.
 */

/* Version of the API a page in the `www` partition may ask for (api_min in its .dbfw header). Raise it when a
 * route or field the page relies on changes. */
#define DB_WEB_API 2 /* 2: slots count from 1, a pairing accept names its "ip" */

/* `status_json` writes the body of /api/status; `state_json` the web socket's state message, which leaves out
 * what changes all the time. */
esp_err_t db_web_start(size_t (*status_json)(char *buf, size_t cap),
                       size_t (*state_json)(char *buf, size_t cap));

/* SHA-256 of the HTTPS certificate, "AB:CD:…"; empty without HTTPS. */
const char *db_web_fingerprint(void);
/* HTTPS serves an uploaded certificate, not its own. */
bool db_web_cert_custom(void);
/* An uploaded certificate is kept but could not be read at this start (memory, flash): the own one serves. */
bool db_web_cert_unread(void);

/* Each web server ran a job since the start: it serves. */
bool db_web_alive(void);

/* The web wizard's step, 1 to 6, kept over restarts; 0 when it does not run. */
int db_web_setup_step(void);

/* The "code" a page translates for an error text it shows to users, or NULL. */
const char *db_web_error_code(const char *msg);

/* PUT /api/config with the password's rights, for setup that does not come over HTTP (Improv on the serial
 * console). Returns the HTTP status and writes the JSON reply. */
int db_web_config(const char *json, char *reply, size_t cap);

/* Send one JSON message to every listener of /api/ws. */
void db_web_broadcast(const char *json);
