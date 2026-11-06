#pragma once

#include <stdbool.h>

#include "esp_err.h"
#include "esp_http_server.h"

/*
 * Login for the web UI. The browser gets a random session cookie (HttpOnly, SameSite=Strict, 12 h idle
 * timeout). Scripts may send the password as HTTP basic auth instead. Every request that changes something
 * must also carry the header X-Deckbridge: 1, which a foreign page cannot add. Failed logins lock the login
 * for a growing time. Sessions live in RAM, so a restart (also after a password change) ends them all.
 *
 * Integrations such as Home Assistant use a token instead (Authorization: Bearer ...). It reaches the
 * WEB_CONTROL routes only, needs no marker header (a browser never sends it by itself), and only its SHA-256
 * is kept.
 */

#include "web.h"

/* True when the request may go on at `need`. Otherwise a 401 or 403 has been sent. */
bool auth_check(httpd_req_t *req, web_level_t need);
/* The request comes with a token, not the password. */
bool auth_by_token(httpd_req_t *req);
void auth_start(void);

esp_err_t auth_tokens_list(httpd_req_t *req);
esp_err_t auth_tokens_add(httpd_req_t *req);
esp_err_t auth_tokens_remove(httpd_req_t *req);

esp_err_t auth_login(httpd_req_t *req);
esp_err_t auth_logout(httpd_req_t *req);
