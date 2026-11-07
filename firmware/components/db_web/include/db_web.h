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

/* The web wizard's step, 1 to 6, kept over restarts; 0 when it does not run. */
int db_web_setup_step(void);
/* Seconds the input lock stays open after PUT /api/input/arm; 0 when it is shut or there is no lock. */
int db_web_input_armed_s(void);

/* Send one JSON message to every listener of /api/ws. */
void db_web_broadcast(const char *json);
