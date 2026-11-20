#pragma once

#include <stdbool.h>

#include "db_ui.h"
#include "esp_err.h"
#include "esp_http_server.h"

/*
 * Login for the web UI. The browser gets a random session cookie (HttpOnly, SameSite=Strict, 12 h idle
 * timeout). Scripts may send the password as HTTP basic auth instead. Every request that changes something
 * must also carry the header X-DeckConnector: 1, which a foreign page cannot add. Failed logins lock the
 * login for a growing time. Up to 16 sessions; a software restart keeps them, a power loss or a new password
 * ends them all.
 *
 * Integrations use a token instead (Authorization: Bearer ...). A control token reaches the WEB_CONTROL
 * routes, a full token also WEB_FULL; neither reaches WEB_ADMIN (the password, the tokens). A token needs no
 * marker header (a browser never sends it by itself), and only its SHA-256 is kept.
 *
 * A request over the setup access point counts as a password login: its WPA password shows only on the deck.
 */

#include "web.h"

/* True when the request may go on at `need`. Otherwise a 401 or 403 has been sent. */
bool auth_check(httpd_req_t *req, web_level_t need);
/* The request comes with a token, not the password. */
bool auth_by_token(httpd_req_t *req);
/* A new session for this request, sent as its cookie. */
void auth_session_cookie(httpd_req_t *req);
/* The request came in over the setup access point. */
bool auth_via_ap(httpd_req_t *req);
/* The password, or a token with full rights. */
bool auth_full(httpd_req_t *req);
void auth_start(void);
/* A new password: every browser logs in again. */
void auth_end_sessions(void);

esp_err_t auth_tokens_list(httpd_req_t *req);
esp_err_t auth_tokens_add(httpd_req_t *req);
esp_err_t auth_tokens_remove(httpd_req_t *req);
/* DELETE /api/token: the token of the request ends itself, as an integration does when it is removed. */
esp_err_t auth_token_remove_self(httpd_req_t *req);

esp_err_t auth_login(httpd_req_t *req);
/* GET /?t=<token>: the one-time login the deck shows as a QR code, then a redirect to / */
esp_err_t auth_qr_login(httpd_req_t *req, const char *token);
esp_err_t auth_logout(httpd_req_t *req);
/* No login: someone at the deck reads the password there, or resets the bridge with two taps on it. */
esp_err_t auth_password_show(httpd_req_t *req);
/* POST asks on the deck, DELETE takes the question back. */
esp_err_t auth_password_reset(httpd_req_t *req);
/* Before setup, any browser within CLAIM_S after the start gets a session without the password, until a
 * password is set or the setup is done. */
esp_err_t auth_setup_claim(httpd_req_t *req);
esp_err_t auth_claim_close(void);
/* Who put a page on the deck: the lockout key, an IPv6 client by its /64. */
uint32_t auth_asker(httpd_req_t *req);
/* Seconds `key` waits before it may put `page` on the deck again: 60 s after its last one there ended without
 * a Yes. auth_raised notes it as the client of the page just shown. */
int auth_paused_s(db_ui_page_t page, uint32_t key);
void auth_raised(db_ui_page_t page, uint32_t key);
/* A websocket checks its login again on every message: a logout, a new password, the end of the setup
 * claim, the closed setup WLAN or a removed token must reach it too. The key names what let it in: its session,
 * its token's hash, the setup WLAN, or the password's generation for basic auth. "" never stays alive. */
#define AUTH_KEY_LEN 66
void auth_login_of(httpd_req_t *req, char key[AUTH_KEY_LEN]);
bool auth_login_alive(const char *key);
/* No login: POST asks on the deck to let this browser in. /<id>: GET polls and logs in once accepted, DELETE
 * takes the request back. */
esp_err_t auth_login_request(httpd_req_t *req);
esp_err_t auth_login_request_id(httpd_req_t *req);
/* Passkeys (WebAuthn, ES256 only), up to 8, each for the host name it was made on: over HTTPS by a name only.
 * A challenge is for one login or registration from one address within 2 min. Logins count like passwords. */
esp_err_t auth_passkey_challenge(httpd_req_t *req);
esp_err_t auth_passkey_login(httpd_req_t *req);
esp_err_t auth_passkey_add(httpd_req_t *req);
esp_err_t auth_passkeys_list(httpd_req_t *req);
esp_err_t auth_passkey_edit(httpd_req_t *req);
/* The Host header names an address or a .local name: not another site's page through DNS rebinding. */
bool auth_host_is_local(httpd_req_t *req);
