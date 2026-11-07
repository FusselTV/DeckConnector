#pragma once

#include <stdbool.h>

#include "esp_err.h"
#include "esp_http_server.h"

/*
 * Login for the web UI. The browser gets a random session cookie (HttpOnly, SameSite=Strict, 12 h idle
 * timeout). Scripts may send the password as HTTP basic auth instead. Every request that changes something
 * must also carry the header X-DeckConnector: 1 (or the old X-Deckbridge: 1), which a foreign page cannot add. Failed logins lock the login
 * for a growing time. Sessions live in RAM, so a restart (also after a password change) ends them all.
 *
 * Integrations use a token instead (Authorization: Bearer ...). A control token reaches the WEB_CONTROL routes, a
 * full token also WEB_FULL; neither reaches WEB_ADMIN (the password, the tokens). A token needs no marker header
 * (a browser never sends it by itself), and only its SHA-256 is kept.
 *
 * A request over the setup access point counts as a password login: its WPA password shows only on the deck.
 */

#include "web.h"

/* True when the request may go on at `need`. Otherwise a 401 or 403 has been sent. */
bool auth_check(httpd_req_t *req, web_level_t need);
/* The request comes with a token, not the password. */
bool auth_by_token(httpd_req_t *req);
/* The request came in over the setup access point. */
bool auth_via_ap(httpd_req_t *req);
/* The password, or a token with full rights. */
bool auth_full(httpd_req_t *req);
void auth_start(void);

esp_err_t auth_tokens_list(httpd_req_t *req);
esp_err_t auth_tokens_add(httpd_req_t *req);
esp_err_t auth_tokens_remove(httpd_req_t *req);

esp_err_t auth_login(httpd_req_t *req);
/* GET /?t=<token>: the one-time login the deck shows as a QR code, then a redirect to / */
esp_err_t auth_qr_login(httpd_req_t *req, const char *token);
esp_err_t auth_logout(httpd_req_t *req);
/* No login: someone at the deck reads the password there, or resets the bridge with two taps on it. */
esp_err_t auth_password_show(httpd_req_t *req);
/* POST asks on the deck, DELETE takes the question back. */
esp_err_t auth_password_reset(httpd_req_t *req);
/* No login: POST asks on the deck to let this browser in. /<id>: GET polls and logs in once accepted, DELETE
 * takes the request back. */
esp_err_t auth_login_request(httpd_req_t *req);
esp_err_t auth_login_request_id(httpd_req_t *req);
