#pragma once

#include <stdbool.h>

#include "esp_err.h"
#include "esp_http_server.h"

/*
 * Login for the web UI. The browser gets a random session cookie (HttpOnly, SameSite=Strict, 12 h idle
 * timeout). Scripts may send the password as HTTP basic auth instead. Every request that changes something
 * must also carry the header X-Deckbridge: 1, which a foreign page cannot add. Failed logins lock the login
 * for a growing time. Sessions live in RAM, so a restart (also after a password change) ends them all.
 */

/* True when the request may go on. Otherwise a 401 has been sent. */
bool auth_check(httpd_req_t *req);

esp_err_t auth_login(httpd_req_t *req);
esp_err_t auth_logout(httpd_req_t *req);
