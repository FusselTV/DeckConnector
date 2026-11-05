#pragma once

#include <stddef.h>

#include "esp_err.h"

/*
 * Web UI and JSON API on port 80 and, with a certificate made on the device, on 443. Behind HTTP basic auth
 * (user admin, the generated password). While the setup access point is on, a DNS server sends every name to
 * the bridge so phones open the page by themselves.
 */

/* `status_json` writes the body of /api/status; `state_json` the web socket's state message, which leaves out
 * what changes all the time. */
esp_err_t db_web_start(size_t (*status_json)(char *buf, size_t cap),
                       size_t (*state_json)(char *buf, size_t cap));

/* SHA-256 of the HTTPS certificate, "AB:CD:…"; empty without HTTPS. */
const char *db_web_fingerprint(void);

/* Send one JSON message to every listener of /api/ws. */
void db_web_broadcast(const char *json);
