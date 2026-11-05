#include "auth.h"

#include <stdio.h>
#include <string.h>

#include "cJSON.h"
#include "db_config.h"
#include "esp_log.h"
#include "esp_random.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "mbedtls/base64.h"
#include "web.h"

#define SESSIONS    4
#define TOKEN_BYTES 32
#define IDLE_US     (12LL * 3600 * 1000000)
#define FREE_TRIES  5
#define MAX_LOCK_S  60
#define COOKIE      "db_session"

static const char *TAG = "auth";

static struct {
    char token[TOKEN_BYTES * 2 + 1];
    int64_t last_used;
} sessions[SESSIONS];

static int failures;
static int64_t locked_until;
static portMUX_TYPE mux = portMUX_INITIALIZER_UNLOCKED;

/* Compare without an early exit, so the time does not tell how many characters were right. */
static bool equal(const char *a, const char *b)
{
    size_t la = strlen(a);
    size_t lb = strlen(b);
    unsigned diff = (unsigned)(la ^ lb);
    for (size_t i = 0; i < la && i < lb; i++) {
        diff |= (unsigned)(a[i] ^ b[i]);
    }
    return diff == 0;
}

static bool session_valid(const char *token)
{
    int64_t now = esp_timer_get_time();
    bool ok = false;
    portENTER_CRITICAL(&mux);
    for (int i = 0; i < SESSIONS; i++) {
        if (sessions[i].token[0] && now - sessions[i].last_used < IDLE_US &&
            equal(sessions[i].token, token)) {
            sessions[i].last_used = now;
            ok = true;
        }
    }
    portEXIT_CRITICAL(&mux);
    return ok;
}

static bool locked(void)
{
    return esp_timer_get_time() < locked_until;
}

/* One counter for the login form and for basic auth, so neither allows unlimited guessing. */
static void count_try(bool ok)
{
    if (ok) {
        failures = 0;
        return;
    }
    failures++;
    if (failures >= FREE_TRIES) {
        int shift = failures - FREE_TRIES < 6 ? failures - FREE_TRIES : 6;
        int secs = 1 << shift;
        locked_until = esp_timer_get_time() + (int64_t)(secs < MAX_LOCK_S ? secs : MAX_LOCK_S) * 1000000;
    }
    ESP_LOGW(TAG, "wrong web password (%d in a row)", failures);
}

static bool basic_auth_ok(httpd_req_t *req)
{
    char given[96] = {0};
    if (httpd_req_get_hdr_value_str(req, "Authorization", given, sizeof(given)) != ESP_OK ||
        strncmp(given, "Basic ", 6) != 0 || locked()) {
        return false;
    }
    char plain[64];
    unsigned char expected[96];
    size_t n = 0;
    int len = snprintf(plain, sizeof(plain), "admin:%s", db_config()->admin_pass);
    mbedtls_base64_encode(expected, sizeof(expected) - 1, &n, (const unsigned char *)plain, (size_t)len);
    expected[n] = '\0';
    bool ok = equal(given + 6, (const char *)expected);
    count_try(ok);
    return ok;
}

bool auth_check(httpd_req_t *req)
{
    /* Browsers send cookies and remembered basic auth also with requests from other sites; those cannot add
     * a custom header, so every change needs it. */
    char marker[4] = {0};
    bool marked = httpd_req_get_hdr_value_str(req, "X-Deckbridge", marker, sizeof(marker)) == ESP_OK;
    bool allowed = req->method == HTTP_GET || marked;
    char token[TOKEN_BYTES * 2 + 1] = {0};
    size_t len = sizeof(token);
    bool cookie = httpd_req_get_cookie_val(req, COOKIE, token, &len) == ESP_OK && session_valid(token);
    if (allowed && (cookie || basic_auth_ok(req))) {
        return true;
    }
    /* No WWW-Authenticate header: the page shows its own login form instead of the browser dialog. */
    send_error(req, "401 Unauthorized", "login required");
    return false;
}

static void new_session(char *out)
{
    uint8_t raw[TOKEN_BYTES];
    esp_fill_random(raw, sizeof(raw));
    for (int i = 0; i < TOKEN_BYTES; i++) {
        snprintf(out + 2 * i, 3, "%02x", raw[i]);
    }
    int64_t now = esp_timer_get_time();
    portENTER_CRITICAL(&mux);
    int slot = 0;
    for (int i = 1; i < SESSIONS; i++) { /* replace the session unused for the longest time */
        if (sessions[i].last_used < sessions[slot].last_used) {
            slot = i;
        }
    }
    strcpy(sessions[slot].token, out);
    sessions[slot].last_used = now;
    portEXIT_CRITICAL(&mux);
}

static bool password_matches(httpd_req_t *req)
{
    char body[128];
    if (read_body(req, body, sizeof(body)) < 0) {
        return false;
    }
    cJSON *root = cJSON_Parse(body);
    const cJSON *pw = cJSON_GetObjectItemCaseSensitive(root, "password");
    bool ok = cJSON_IsString(pw) && equal(pw->valuestring, db_config()->admin_pass);
    cJSON_Delete(root);
    return ok;
}

esp_err_t auth_login(httpd_req_t *req)
{
    int64_t now = esp_timer_get_time();
    if (now < locked_until) {
        char msg[80];
        snprintf(msg, sizeof(msg), "{\"ok\":false,\"error\":\"locked\",\"retry\":%lld}",
                 (long long)((locked_until - now) / 1000000 + 1));
        httpd_resp_set_status(req, "429 Too Many Requests");
        return send_json(req, msg);
    }
    bool ok = password_matches(req);
    count_try(ok);
    if (!ok) {
        return send_error(req, "403 Forbidden", "wrong password");
    }
    char token[TOKEN_BYTES * 2 + 1];
    new_session(token);
    char cookie[128];
    snprintf(cookie, sizeof(cookie), COOKIE "=%s; Path=/; HttpOnly; SameSite=Strict; Max-Age=43200", token);
    httpd_resp_set_hdr(req, "Set-Cookie", cookie);
    return send_json(req, "{\"ok\":true}");
}

esp_err_t auth_logout(httpd_req_t *req)
{
    char token[TOKEN_BYTES * 2 + 1] = {0};
    size_t len = sizeof(token);
    if (httpd_req_get_cookie_val(req, COOKIE, token, &len) == ESP_OK) {
        portENTER_CRITICAL(&mux);
        for (int i = 0; i < SESSIONS; i++) {
            if (sessions[i].token[0] && equal(sessions[i].token, token)) {
                sessions[i].token[0] = '\0';
            }
        }
        portEXIT_CRITICAL(&mux);
    }
    httpd_resp_set_hdr(req, "Set-Cookie", COOKIE "=; Path=/; HttpOnly; SameSite=Strict; Max-Age=0");
    return send_json(req, "{\"ok\":true}");
}
