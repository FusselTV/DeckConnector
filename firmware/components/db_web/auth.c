#include "auth.h"

#include <arpa/inet.h>
#include <ctype.h>
#include <stdio.h>
#include <string.h>

#include "cJSON.h"
#include "db_config.h"
#include "db_json.h"
#include "db_net.h"
#include "db_passkey.h"
#include "db_ui.h"
#include "esp_attr.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_random.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "mbedtls/base64.h"
#include "mbedtls/platform_util.h"
#include "mbedtls/psa_util.h"
#include "nvs.h"
#include "psa/crypto.h"
#include "web.h"

#define SESSIONS    16
#define TOKEN_BYTES 32
#define IDLE_US     (12LL * 3600 * 1000000)
#define FREE_TRIES  5
#define MAX_LOCK_S  60
#define ALL_TRIES   30 /* wrong passwords per minute from all clients together */
/* One name per scheme: a browser drops a plain cookie named like a Secure one it holds, the old "db_session"
 * included. */
#define COOKIE(req) (web_tls(req) ? "__Host-dc_session" : "dc_session")
#define API_TOKENS  8

static const char *TAG = "auth";

#if CONFIG_IDF_TARGET_LINUX
#undef RTC_NOINIT_ATTR
#define RTC_NOINIT_ATTR
#endif

/* In LP RAM, which a software restart keeps and a power loss scrambles. Across a restart last_used holds the
 * idle age instead, and `kept_sum` (SHA-256 of the table and the password's hash) proves the table was
 * written by the shutdown handler for this password: a new password or garbage after power on drops every
 * session. */
static RTC_NOINIT_ATTR struct {
    char token[TOKEN_BYTES * 2 + 1];
    int64_t last_used;
    bool claimed; /* from the first-browser setup: ends with the setup */
} sessions[SESSIONS];
static RTC_NOINIT_ATTR uint8_t kept_sum[32];
static uint8_t pass_sha[32]; /* of the password this boot started with: the live one already is the new one */

/* Failed tries per client address: one guesser cannot lock out the others. The oldest entry makes room. */
#define TRIERS 8
static struct {
    uint32_t ip;
    int failures;
    int64_t locked_until;
} triers[TRIERS];
static int64_t all_since; /* the minute ALL_TRIES counts in: one guesser on many addresses */
static int all_failures;
/* Addresses that logged in this boot skip the ALL_TRIES lock, so a guesser on many addresses cannot keep the
 * owner out for good. A new device of the owner's still gets in through a login request on the deck. */
#define GOOD_MAX 4
static uint32_t good[GOOD_MAX];
static int next_good;
static portMUX_TYPE mux = portMUX_INITIALIZER_UNLOCKED;

/* Under mux. */
static bool known_good(uint32_t ip)
{
    bool known = false;
    for (int i = 0; i < GOOD_MAX; i++) {
        known |= good[i] == ip;
    }
    return known;
}

typedef struct {
    char name[24];
    uint8_t sha[32];
} api_token_t;
static api_token_t tokens[API_TOKENS]; /* in NVS "auth", not in the settings: a backup must not carry them */
static uint8_t full_rights;            /* bit i: token i reaches WEB_FULL, else WEB_CONTROL only */
static bool tokens_unread;             /* a save now would overwrite the stored tokens */

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

static bool auth_marked(httpd_req_t *req)
{
    char marker[4];
    return httpd_req_get_hdr_value_str(req, "X-DeckConnector", marker, sizeof(marker)) == ESP_OK;
}

static bool session_valid(const char *token)
{
    int64_t now = esp_timer_get_time();
    bool ok = false;
    portENTER_CRITICAL(&mux);
    for (int i = 0; i < SESSIONS; i++) {
        if (sessions[i].token[0] && now - sessions[i].last_used < IDLE_US &&
            !(sessions[i].claimed && db_config()->setup_done) && equal(sessions[i].token, token)) {
            sessions[i].last_used = now;
            ok = true;
        }
    }
    portEXIT_CRITICAL(&mux);
    return ok;
}

/* The peer's address, or with `local` the address the request came in on. */
static uint32_t sock_ip(httpd_req_t *req, bool local)
{
    struct sockaddr_in6 addr; /* lwIP reports IPv4 peers as IPv4-mapped IPv6 */
    socklen_t len = sizeof(addr);
    int fd = httpd_req_to_sockfd(req);
    if ((local ? getsockname(fd, (struct sockaddr *)&addr, &len)
               : getpeername(fd, (struct sockaddr *)&addr, &len)) != 0) {
        return 0;
    }
    if (addr.sin6_family == AF_INET) {
        return ((struct sockaddr_in *)&addr)->sin_addr.s_addr;
    }
    if (!IN6_IS_ADDR_V4MAPPED(&addr.sin6_addr)) {
        return 0;
    }
    uint32_t v4;
    memcpy(&v4, (const uint8_t *)&addr.sin6_addr + 12, 4); /* the last 4 bytes of ::ffff:a.b.c.d */
    return v4;
}

static uint32_t peer_ip(httpd_req_t *req)
{
    return sock_ip(req, false);
}

bool auth_via_ap(httpd_req_t *req)
{
    db_net_status_t n;
    db_net_status(&n);
    if (!n.ap_active) {
        return false;
    }
    uint32_t ap = inet_addr(n.ap_ip);
    uint32_t mask = htonl(0xffffff00);
    /* lwIP also accepts the AP's address on the LAN, from a host that routes 192.168.4.0/24 there; its
     * answers to a peer in the AP's subnet go out over the AP, so that host never completes the connection */
    return sock_ip(req, true) == ap && (peer_ip(req) & mask) == (ap & mask);
}

/* The entry of this client, made when missing. Under mux. */
static int trier(uint32_t ip)
{
    int oldest = 0;
    for (int i = 0; i < TRIERS; i++) {
        if (triers[i].ip == ip) {
            return i;
        }
        oldest = triers[i].locked_until < triers[oldest].locked_until ? i : oldest;
    }
    triers[oldest] = (typeof(triers[0])){.ip = ip};
    return oldest;
}

/* Seconds this client must still wait, 0 when it may try. */
static int locked_s(httpd_req_t *req)
{
    uint32_t ip = peer_ip(req);
    int64_t now = esp_timer_get_time();
    portENTER_CRITICAL(&mux);
    int64_t until = triers[trier(ip)].locked_until;
    if (!known_good(ip) && all_failures >= ALL_TRIES && all_since + 60000000 > until) {
        until = all_since + 60000000;
    }
    portEXIT_CRITICAL(&mux);
    return now < until ? (int)((until - now) / 1000000 + 1) : 0;
}

/* One count per client for the login form and for basic auth, so neither allows unlimited guessing. Only
 * passwords count towards ALL_TRIES: a 256-bit token cannot be guessed. */
static void count_try(httpd_req_t *req, bool ok, bool password)
{
    uint32_t ip = peer_ip(req);
    int64_t now = esp_timer_get_time();
    portENTER_CRITICAL(&mux);
    if (password && !ok && now - all_since >= 60000000) {
        all_since = now;
        all_failures = 0;
    }
    all_failures += password && !ok;
    if (ok && !known_good(ip)) {
        good[next_good++ % GOOD_MAX] = ip;
    }
    int i = trier(ip);
    int failures = ok ? 0 : ++triers[i].failures;
    triers[i].failures = failures;
    if (failures >= FREE_TRIES) {
        int shift = failures - FREE_TRIES < 6 ? failures - FREE_TRIES : 6;
        int secs = 1 << shift;
        triers[i].locked_until = now + (int64_t)(secs < MAX_LOCK_S ? secs : MAX_LOCK_S) * 1000000;
    }
    portEXIT_CRITICAL(&mux);
    if (!ok) {
        ESP_LOGW(TAG, "wrong web password (%d in a row)", failures);
    }
}

static bool basic_auth_ok(httpd_req_t *req)
{
    char given[96] = {0};
    if (httpd_req_get_hdr_value_str(req, "Authorization", given, sizeof(given)) != ESP_OK ||
        strncmp(given, "Basic ", 6) != 0 || locked_s(req)) {
        return false;
    }
    char plain[64];
    unsigned char expected[96];
    size_t n = 0;
    int len = snprintf(plain, sizeof(plain), "admin:%s", db_config()->admin_pass);
    mbedtls_base64_encode(expected, sizeof(expected) - 1, &n, (const unsigned char *)plain, (size_t)len);
    expected[n] = '\0';
    bool ok = equal(given + 6, (const char *)expected);
    /* a crash dump carries this stack */
    mbedtls_platform_zeroize(given, sizeof(given));
    mbedtls_platform_zeroize(plain, sizeof(plain));
    mbedtls_platform_zeroize(expected, sizeof(expected));
    count_try(req, ok, true);
    return ok;
}

static void sha256(const char *text, uint8_t out[32])
{
    size_t len;
    psa_crypto_init();
    psa_hash_compute(PSA_ALG_SHA_256, (const uint8_t *)text, strlen(text), out, 32, &len);
}

bool auth_by_token(httpd_req_t *req)
{
    char given[16] = {0};
    httpd_req_get_hdr_value_str(req, "Authorization", given, sizeof(given)); /* too long is fine: a prefix */
    return strncmp(given, "Bearer ", 7) == 0;
}

/* The index of the request's token, or -1. */
static int token_index(httpd_req_t *req)
{
    char given[96] = {0};
    if (httpd_req_get_hdr_value_str(req, "Authorization", given, sizeof(given)) != ESP_OK) {
        return -1;
    }
    uint8_t sha[32];
    sha256(given + 7, sha);
    mbedtls_platform_zeroize(given, sizeof(given));
    int found = -1;
    portENTER_CRITICAL(&mux);
    for (int i = 0; i < API_TOKENS; i++) {
        unsigned d = tokens[i].name[0] ? 0 : 1;
        for (int k = 0; k < 32; k++) {
            d |= tokens[i].sha[k] ^ sha[k];
        }
        found = d == 0 ? i : found;
    }
    portEXIT_CRITICAL(&mux);
    if (found < 0) {
        count_try(req, false, false); /* a hit must not reset the count: only the password does */
    }
    return found;
}

bool auth_full(httpd_req_t *req)
{
    if (!auth_by_token(req)) {
        return true;
    }
    int i = token_index(req);
    return i >= 0 && (full_rights >> i & 1);
}

/* true when the client is locked out after wrong guesses; then it has its 429 with the wait. */
static bool locked(httpd_req_t *req)
{
    int wait = locked_s(req);
    if (wait) {
        char msg[80];
        snprintf(msg, sizeof(msg), "{\"ok\":false,\"error\":\"locked\",\"code\":\"locked\",\"retry\":%d}",
                 wait);
        httpd_resp_set_status(req, "429 Too Many Requests");
        send_json(req, msg);
    }
    return wait;
}

bool auth_check(httpd_req_t *req, web_level_t need)
{
    char given[8];
    bool bearer = auth_by_token(req);
    if (!bearer &&
        httpd_req_get_hdr_value_str(req, "Authorization", given, sizeof(given)) != ESP_ERR_NOT_FOUND &&
        locked(req)) {
        return false; /* a token is never locked out: it is checked before any lockout */
    }
    if (bearer) {
        int i = token_index(req);
        if (i < 0) {
            send_error(req, "401 Unauthorized", "unknown token");
            return false;
        }
        if (need == WEB_ADMIN) {
            send_error(req, "403 Forbidden", "needs the password, not a token");
            return false;
        }
        if (need == WEB_FULL && !(full_rights >> i & 1)) {
            send_error(req, "403 Forbidden", "this token may only control the deck");
            return false;
        }
        return true;
    }
    /* Browsers send cookies and remembered basic auth also with requests from other sites; those cannot add
     * a custom header, so every change needs it. */
    bool marked = auth_marked(req);
    bool allowed = req->method == HTTP_GET || marked;
    char token[TOKEN_BYTES * 2 + 1] = {0};
    size_t len = sizeof(token);
    bool cookie = httpd_req_get_cookie_val(req, COOKIE(req), token, &len) == ESP_OK && session_valid(token);
    if (allowed && (cookie || auth_via_ap(req) || basic_auth_ok(req))) {
        return true;
    }
    /* No WWW-Authenticate header: the page shows its own login form instead of the browser dialog. */
    send_error(req, "401 Unauthorized", "login required");
    return false;
}

static void new_session(char *out, bool claimed)
{
    uint8_t raw[TOKEN_BYTES];
    esp_fill_random(raw, sizeof(raw));
    db_hex(out, raw, sizeof(raw));
    int64_t now = esp_timer_get_time();
    portENTER_CRITICAL(&mux);
    int slot = 0;
    for (int i = 1; i < SESSIONS; i++) { /* a free slot, else the session unused for the longest time */
        int64_t a = sessions[i].token[0] ? sessions[i].last_used : INT64_MIN;
        int64_t b = sessions[slot].token[0] ? sessions[slot].last_used : INT64_MIN;
        slot = a < b ? i : slot;
    }
    strcpy(sessions[slot].token, out);
    sessions[slot].last_used = now;
    sessions[slot].claimed = claimed;
    portEXIT_CRITICAL(&mux);
}

void auth_end_sessions(void)
{
    portENTER_CRITICAL(&mux);
    memset(sessions, 0, sizeof(sessions));
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
    mbedtls_platform_zeroize(body, sizeof(body));
    return ok;
}

static void set_cookie(httpd_req_t *req, bool claimed)
{
    /* the header must live until the reply is sent; one slot per server, each runs its handlers one at a time
     */
    static char cookies[2][160];
    char *cookie = cookies[web_tls(req)];
    /* 30 days: the 12 h idle limit is the bridge's, and a browser must not end a session in use any sooner */
    char token[TOKEN_BYTES * 2 + 1];
    new_session(token, claimed);
    snprintf(cookie, sizeof(cookies[0]), "%s=%s; Path=/; HttpOnly; SameSite=Strict; Max-Age=2592000%s",
             COOKIE(req), token, web_tls(req) ? "; Secure" : "");
    httpd_resp_set_hdr(req, "Set-Cookie", cookie);
}

void auth_session_cookie(httpd_req_t *req)
{
    set_cookie(req, false);
}

bool auth_session_of(httpd_req_t *req, char *out, size_t cap)
{
    return httpd_req_get_cookie_val(req, COOKIE(req), out, &cap) == ESP_OK && session_valid(out);
}

bool auth_session_alive(const char *token)
{
    int64_t now = esp_timer_get_time();
    bool ok = false;
    portENTER_CRITICAL(&mux);
    for (int i = 0; i < SESSIONS;
         i++) { /* no last_used update: an open socket alone must not keep a session */
        ok |= sessions[i].token[0] && now - sessions[i].last_used < IDLE_US &&
              !(sessions[i].claimed && db_config()->setup_done) && equal(sessions[i].token, token);
    }
    portEXIT_CRITICAL(&mux);
    return ok;
}

esp_err_t auth_login(httpd_req_t *req)
{
    /* the marker header, as for every change: a page from another site cannot send it, so it cannot guess
     * passwords from a browser on the LAN or lock its user out */
    if (!auth_marked(req)) {
        return send_error(req, "403 Forbidden", "X-DeckConnector header missing");
    }
    if (locked(req)) {
        return ESP_OK;
    }
    bool ok = password_matches(req);
    count_try(req, ok, true);
    if (!ok) {
        return send_error(req, "403 Forbidden", "wrong password");
    }
    auth_session_cookie(req);
    return send_json(req, "{\"ok\":true}");
}

esp_err_t auth_password_show(httpd_req_t *req)
{
    if (!auth_marked(req)) {
        return send_error(req, "403 Forbidden", "X-DeckConnector header missing");
    }
    int decks = db_ui_show_password();
    if (!decks) {
        return send_error(req, "409 Conflict", "no deck attached");
    }
    char reply[32];
    snprintf(reply, sizeof(reply), "{\"ok\":true,\"decks\":%d}", decks);
    return send_json(req, reply);
}

esp_err_t auth_password_reset(httpd_req_t *req)
{
    if (!auth_marked(req)) {
        return send_error(req, "403 Forbidden", "X-DeckConnector header missing");
    }
    if (req->method == HTTP_DELETE) {
        db_ui_cancel_reset();
        return send_json(req, "{\"ok\":true}");
    }
    int left = db_ui_ask_reset();
    if (left < 0) {
        return send_error(req, "409 Conflict", "no deck attached");
    }
    char reply[32];
    snprintf(reply, sizeof(reply), "{\"ok\":true,\"s\":%d}", left);
    return send_json(req, reply);
}

#define ASK_S        60
#define ASK_AGAIN_US 3000000
#define ASKS         4 /* replaced ids still read "denied" until this many newer ones came */
#define WHO_CHARS    48
#define ASK_ID       32 /* hex digits, 128 random bits */

typedef enum { ASK_PENDING, ASK_DENIED, ASK_EXPIRED, ASK_USED } ask_state_t;
static const char *const ask_names[] = {"pending", "denied", "expired", "used"};
static struct {
    char id[ASK_ID + 1];
    int64_t until;
    ask_state_t state;
} asks[ASKS]; /* under mux */
static int ask_cur = -1;
static int64_t ask_last;

static bool who_ok(const cJSON *who)
{
    if (!cJSON_IsString(who) || strlen(who->valuestring) > WHO_CHARS * 2) {
        return false;
    }
    int chars = 0;
    for (const unsigned char *c = (const unsigned char *)who->valuestring; *c; c++) {
        if (*c < 0x20 || *c == 0x7f) {
            return false;
        }
        chars += (*c & 0xc0) != 0x80;
    }
    return chars <= WHO_CHARS;
}

esp_err_t auth_login_request(httpd_req_t *req)
{
    if (!auth_marked(req)) {
        return send_error(req, "403 Forbidden", "X-DeckConnector header missing");
    }
    char body[256];
    char who[WHO_CHARS * 2 + 1] = "";
    cJSON *root = read_body(req, body, sizeof(body)) < 0 ? NULL : cJSON_Parse(body);
    const cJSON *w = cJSON_GetObjectItemCaseSensitive(root, "who");
    bool ok = root && (!w || who_ok(w));
    if (ok && w) {
        strlcpy(who, w->valuestring, sizeof(who));
    }
    cJSON_Delete(root);
    if (!ok) {
        return send_error(req, "400 Bad Request", "who: a label of up to 48 characters");
    }
    int64_t now = esp_timer_get_time();
    portENTER_CRITICAL(&mux);
    bool soon = ask_cur >= 0 && now - ask_last < ASK_AGAIN_US;
    portEXIT_CRITICAL(&mux);
    if (soon) {
        return send_error(req, "429 Too Many Requests", "one login request per 3 s");
    }
    if (!db_ui_ask_login(peer_ip(req), who, ASK_S)) {
        return send_error(req, "409 Conflict", "no deck attached");
    }
    uint8_t raw[ASK_ID / 2];
    char id[ASK_ID + 1];
    esp_fill_random(raw, sizeof(raw));
    db_hex(id, raw, sizeof(raw));
    portENTER_CRITICAL(&mux);
    if (ask_cur >= 0 && asks[ask_cur].state == ASK_PENDING) {
        asks[ask_cur].state = ASK_DENIED;
    }
    ask_cur = (ask_cur + 1) % ASKS;
    strcpy(asks[ask_cur].id, id);
    asks[ask_cur].until = now + ASK_S * 1000000LL;
    asks[ask_cur].state = ASK_PENDING;
    ask_last = now;
    portEXIT_CRITICAL(&mux);
    char reply[64];
    snprintf(reply, sizeof(reply), "{\"id\":\"%s\",\"s\":%d}", id, ASK_S);
    return send_json(req, reply);
}

/* GET answers the state and, once, logs in when the deck accepted; DELETE takes the request back. */
esp_err_t auth_login_request_id(httpd_req_t *req)
{
    if (req->method == HTTP_DELETE && !auth_marked(req)) {
        return send_error(req, "403 Forbidden", "X-DeckConnector header missing");
    }
    const char *given = req->uri + strlen("/api/login/request/");
    char id[ASK_ID + 1] = "";
    size_t len = strcspn(given, "?");
    if (len == ASK_ID) {
        memcpy(id, given, len);
        id[len] = '\0';
    }
    int found = -1;
    portENTER_CRITICAL(&mux);
    for (int i = 0; i < ASKS; i++) {
        bool same = asks[i].id[0] && id[0] && equal(asks[i].id, id); /* every one compared: no early exit */
        found = same ? i : found;
    }
    bool current = found >= 0 && found == ask_cur && asks[found].state == ASK_PENDING;
    portEXIT_CRITICAL(&mux);
    if (found < 0) {
        return send_error(req, "404 Not Found", "no such login request");
    }
    if (req->method == HTTP_DELETE) {
        portENTER_CRITICAL(&mux);
        if (asks[found].state == ASK_PENDING) {
            asks[found].state = ASK_DENIED;
        }
        portEXIT_CRITICAL(&mux);
        if (current) {
            db_ui_end_login();
        }
        return send_json(req, "{\"ok\":true}");
    }
    int answer = current ? db_ui_login_answer() : -1;
    int64_t now = esp_timer_get_time();
    bool accepted = false;
    portENTER_CRITICAL(&mux);
    int64_t left = asks[found].until - now;
    if (asks[found].state == ASK_PENDING) {
        /* the deck takes no answer after its page ran out; a poll a moment later still gets the login */
        if (answer == 1 && left > -5000000) {
            accepted = true;
            asks[found].state = ASK_USED;
        } else if (answer == 0) {
            asks[found].state = ASK_DENIED;
        } else if (left <= 0) {
            asks[found].state = ASK_EXPIRED;
        }
    }
    ask_state_t state = asks[found].state;
    portEXIT_CRITICAL(&mux);
    if (accepted) {
        ESP_LOGI(TAG, "login accepted on the deck");
        auth_session_cookie(req);
    }
    char reply[48];
    snprintf(reply, sizeof(reply), "{\"state\":\"%s\",\"s\":%d}", accepted ? "accepted" : ask_names[state],
             state == ASK_PENDING && left > 0 ? (int)((left + 999999) / 1000000) : 0);
    return send_json(req, reply);
}

#define CLAIM_S 1800
static int64_t started; /* esp_timer counts from the host's start in the simulator, not from ours */

/* Open only where a first start or a factory reset wrote 0; a device from an older firmware has no key:
 * closed */
static bool claim_open(void)
{
    nvs_handle_t h;
    uint8_t used = 1;
    if (nvs_open("web", NVS_READONLY, &h) == ESP_OK) {
        nvs_get_u8(h, "claimed", &used);
        nvs_close(h);
    }
    return !used;
}

esp_err_t auth_claim_close(void)
{
    nvs_handle_t h;
    esp_err_t err = nvs_open("web", NVS_READWRITE, &h);
    if (err == ESP_OK) {
        err = nvs_set_u8(h, "claimed", 1);
        err = err == ESP_OK ? nvs_commit(h) : err;
        nvs_close(h);
    }
    return err;
}

/* DNS rebinding: another site's page could reach us under its own name, so only an address or name.local */
bool auth_host_is_local(httpd_req_t *req)
{
    char host[72];
    if (httpd_req_get_hdr_value_str(req, "Host", host, sizeof(host)) != ESP_OK) {
        return false;
    }
    if (host[0] == '[') {
        return true;
    }
    char *colon = strchr(host, ':');
    if (colon) {
        *colon = '\0';
    }
    size_t n = strlen(host);
    if (n && host[n - 1] == '.') {
        host[--n] = '\0';
    }
    if (n > 6 && strcasecmp(host + n - 6, ".local") == 0) {
        return true;
    }
    return n && strspn(host, "0123456789.") == n;
}

esp_err_t auth_setup_claim(httpd_req_t *req)
{
    static bool busy;
    if (!auth_marked(req) || !auth_host_is_local(req)) {
        return send_error(req, "403 Forbidden", "X-DeckConnector header or local address missing");
    }
    if (db_config()->setup_done) {
        return send_error(req, "409 Conflict", "set up");
    }
    if (esp_timer_get_time() - started > CLAIM_S * 1000000LL) {
        return send_error(req, "409 Conflict", "too late");
    }
    portENTER_CRITICAL(&mux);
    bool first = !busy;
    busy = true;
    portEXIT_CRITICAL(&mux);
    if (!first || !claim_open()) {
        return send_error(req, "409 Conflict", "claimed");
    }
    if (auth_claim_close() != ESP_OK) {
        return send_error(req, "500 Internal Server Error", "saving failed");
    }
    struct in_addr ip = {.s_addr = peer_ip(req)};
    ESP_LOGW(TAG, "setup taken over without the password by %s", inet_ntoa(ip));
    set_cookie(req, true);
    return send_json(req, "{\"ok\":true}");
}

esp_err_t auth_qr_login(httpd_req_t *req, const char *token)
{
    if (db_ui_login_take(token)) {
        auth_session_cookie(req);
    }
    httpd_resp_set_status(req, "302 Found");
    httpd_resp_set_hdr(req, "Location", "/"); /* the token leaves the address bar and the history */
    return httpd_resp_send(req, NULL, 0);
}

esp_err_t auth_logout(httpd_req_t *req)
{
    char token[TOKEN_BYTES * 2 + 1] = {0};
    size_t len = sizeof(token);
    if (httpd_req_get_cookie_val(req, COOKIE(req), token, &len) == ESP_OK) {
        portENTER_CRITICAL(&mux);
        for (int i = 0; i < SESSIONS; i++) {
            if (sessions[i].token[0] && equal(sessions[i].token, token)) {
                sessions[i].token[0] = '\0';
            }
        }
        portEXIT_CRITICAL(&mux);
    }
    httpd_resp_set_hdr(req, "Set-Cookie",
                       web_tls(req)
                           ? "__Host-dc_session=; Path=/; HttpOnly; SameSite=Strict; Max-Age=0; Secure"
                           : "dc_session=; Path=/; HttpOnly; SameSite=Strict; Max-Age=0");
    return send_json(req, "{\"ok\":true}");
}

#define PASSKEYS     8
#define CHALLENGES   4
#define CHALLENGE_US (120 * 1000000LL)
#define WEBAUTHN_MAX 3072

typedef struct {
    uint8_t id[128];
    uint8_t id_len;
    bool uv; /* made with user verification: every login needs it too */
    uint8_t xy[64];
    char rp[64];
    char name[24];
    uint32_t count;
} passkey_t;
static passkey_t passkeys[PASSKEYS]; /* in NVS "auth" like the tokens */

static struct {
    uint8_t c[32];
    int64_t until;
    uint32_t ip;
    bool login;
} challenges[CHALLENGES]; /* under mux */

static esp_err_t save_passkeys(void)
{
    nvs_handle_t h;
    esp_err_t err = nvs_open("auth", NVS_READWRITE, &h);
    if (err == ESP_OK) {
        err = nvs_set_blob(h, "passkeys", passkeys, sizeof(passkeys));
        err = err == ESP_OK ? nvs_commit(h) : err;
        nvs_close(h);
    }
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "passkeys not saved: %s", esp_err_to_name(err));
    }
    return err;
}

static void load_passkeys(void)
{
    nvs_handle_t h;
    size_t len = sizeof(passkeys);
    esp_err_t err = nvs_open("auth", NVS_READONLY, &h);
    if (err == ESP_OK) {
        err = nvs_get_blob(h, "passkeys", passkeys, &len);
        nvs_close(h);
    }
    if (err != ESP_OK) {
        memset(passkeys, 0, sizeof(passkeys));
    }
}

/* A passkey belongs to the name the page was opened by. Browsers allow one only over HTTPS without warnings,
 * and never for an address. */
static bool passkey_host(httpd_req_t *req, char rp[64], char origin[80])
{
    char host[72];
    if (httpd_req_get_hdr_value_str(req, "Host", host, sizeof(host)) != ESP_OK || host[0] == '[') {
        return false;
    }
    snprintf(origin, 80, "%s://%s", web_tls(req) ? "https" : "http", host);
    host[strcspn(host, ":")] = '\0';
    size_t n = strlen(host);
    strlcpy(rp, host, 64);
#if CONFIG_IDF_TARGET_LINUX
    bool secure =
        web_tls(req) || strcmp(host, "localhost") == 0; /* the simulator: browsers trust localhost */
#else
    bool secure = web_tls(req);
#endif
    return secure && n && n < 64 && strspn(host, "0123456789.") != n;
}

static bool passkey_refused(httpd_req_t *req, char rp[64], char origin[80])
{
    if (!auth_marked(req)) {
        send_error(req, "403 Forbidden", "X-DeckConnector header missing");
        return true;
    }
    if (!passkey_host(req, rp, origin)) {
        send_error(req, "403 Forbidden", "passkeys need HTTPS and a name, not an address");
        return true;
    }
    return false;
}

/* POST {"login": true|false}: a challenge for one passkey login or registration from this address. */
esp_err_t auth_passkey_challenge(httpd_req_t *req)
{
    char rp[64], origin[80], body[32];
    if (passkey_refused(req, rp, origin)) {
        return ESP_OK;
    }
    cJSON *root = read_body(req, body, sizeof(body)) < 0 ? NULL : cJSON_Parse(body);
    bool login = cJSON_IsTrue(cJSON_GetObjectItemCaseSensitive(root, "login"));
    cJSON_Delete(root);
    uint8_t c[32];
    esp_fill_random(c, sizeof(c));
    uint32_t ip = peer_ip(req);
    int64_t now = esp_timer_get_time();
    portENTER_CRITICAL(&mux);
    int slot = 0;
    for (int i = 0; i < CHALLENGES; i++) { /* one per address and kind, else the oldest makes room */
        if (challenges[i].ip == ip && challenges[i].login == login) {
            slot = i;
            break;
        }
        slot = challenges[i].until < challenges[slot].until ? i : slot;
    }
    memcpy(challenges[slot].c, c, sizeof(c));
    challenges[slot].until = now + CHALLENGE_US;
    challenges[slot].ip = ip;
    challenges[slot].login = login;
    portEXIT_CRITICAL(&mux);
    char reply[96];
    snprintf(reply, sizeof(reply), "{\"challenge\":\"");
    db_hex(reply + strlen(reply), c, sizeof(c));
    strlcat(reply, "\"}", sizeof(reply));
    return send_json(req, reply);
}

/* Used up by any answer that names it, right or wrong. */
static bool take_challenge(const uint8_t c[32], uint32_t ip, bool login)
{
    int64_t now = esp_timer_get_time();
    bool ok = false;
    portENTER_CRITICAL(&mux);
    for (int i = 0; i < CHALLENGES; i++) {
        if (challenges[i].until && memcmp(challenges[i].c, c, 32) == 0) {
            ok = challenges[i].ip == ip && challenges[i].login == login && now < challenges[i].until;
            memset(&challenges[i], 0, sizeof(challenges[i]));
        }
    }
    portEXIT_CRITICAL(&mux);
    return ok;
}

typedef struct {
    uint8_t id[128], client[1024], ad[512], sig[80], spki[96];
    int id_len, client_len, ad_len, sig_len, spki_len;
    char name[24];
    double alg;
} webauthn_t;

static int field(const cJSON *root, const char *key, uint8_t *out, size_t cap)
{
    const cJSON *v = cJSON_GetObjectItemCaseSensitive(root, key);
    return cJSON_IsString(v) ? db_b64url(v->valuestring, out, cap) : -1;
}

/* The reply of navigator.credentials, each binary part base64url. */
static bool read_webauthn(httpd_req_t *req, webauthn_t *w)
{
    char *body = heap_caps_malloc(WEBAUTHN_MAX, MALLOC_CAP_SPIRAM);
    cJSON *root = body && read_body(req, body, WEBAUTHN_MAX) >= 0 ? cJSON_Parse(body) : NULL;
    heap_caps_free(body);
    w->id_len = field(root, "id", w->id, sizeof(w->id));
    w->client_len = field(root, "clientDataJSON", w->client, sizeof(w->client));
    w->ad_len = field(root, "authenticatorData", w->ad, sizeof(w->ad));
    w->sig_len = field(root, "signature", w->sig, sizeof(w->sig));
    w->spki_len = field(root, "publicKey", w->spki, sizeof(w->spki));
    const cJSON *alg = cJSON_GetObjectItemCaseSensitive(root, "publicKeyAlgorithm");
    w->alg = cJSON_IsNumber(alg) ? alg->valuedouble : 0;
    const cJSON *name = cJSON_GetObjectItemCaseSensitive(root, "name");
    w->name[0] = '\0';
    if (cJSON_IsString(name) && strlen(name->valuestring) < sizeof(w->name)) {
        strlcpy(w->name, name->valuestring, sizeof(w->name));
    }
    cJSON_Delete(root);
    return root && w->id_len > 0 && w->client_len > 0 && w->ad_len > 0;
}

/* clientDataJSON as the authenticator signed it: the kind, a challenge of ours, the page's origin. */
static bool client_ok(httpd_req_t *req, const webauthn_t *w, bool login, const char *origin)
{
    cJSON *c = cJSON_ParseWithLength((const char *)w->client, (size_t)w->client_len);
    const cJSON *type = cJSON_GetObjectItemCaseSensitive(c, "type");
    const cJSON *ch = cJSON_GetObjectItemCaseSensitive(c, "challenge");
    const cJSON *o = cJSON_GetObjectItemCaseSensitive(c, "origin");
    uint8_t raw[33];
    bool ok = cJSON_IsString(ch) && db_b64url(ch->valuestring, raw, sizeof(raw)) == 32 &&
              take_challenge(raw, peer_ip(req), login) && cJSON_IsString(type) &&
              strcmp(type->valuestring, login ? "webauthn.get" : "webauthn.create") == 0 &&
              cJSON_IsString(o) && strcmp(o->valuestring, origin) == 0 &&
              !cJSON_IsTrue(cJSON_GetObjectItemCaseSensitive(c, "crossOrigin"));
    cJSON_Delete(c);
    return ok;
}

static bool rp_ok(const db_authdata_t *a, const char *rp)
{
    uint8_t h[32];
    sha256(rp, h);
    return memcmp(a->rp_hash, h, 32) == 0 && (a->flags & DB_AD_UP);
}

/* ES256 over authenticatorData || SHA-256(clientDataJSON). */
static bool signed_by(const passkey_t *pk, const webauthn_t *w)
{
    uint8_t msg[sizeof(w->ad) + 32], point[65] = {0x04}, sig[64];
    size_t n, sig_n;
    memcpy(point + 1, pk->xy, 64);
    memcpy(msg, w->ad, (size_t)w->ad_len);
    psa_key_attributes_t attr = PSA_KEY_ATTRIBUTES_INIT;
    psa_set_key_type(&attr, PSA_KEY_TYPE_ECC_PUBLIC_KEY(PSA_ECC_FAMILY_SECP_R1));
    psa_set_key_bits(&attr, 256);
    psa_set_key_usage_flags(&attr, PSA_KEY_USAGE_VERIFY_MESSAGE);
    psa_set_key_algorithm(&attr, PSA_ALG_ECDSA(PSA_ALG_SHA_256));
    psa_key_id_t key;
    if (w->sig_len <= 0 || psa_crypto_init() != PSA_SUCCESS ||
        psa_hash_compute(PSA_ALG_SHA_256, w->client, (size_t)w->client_len, msg + w->ad_len, 32, &n) !=
            PSA_SUCCESS ||
        mbedtls_ecdsa_der_to_raw(256, w->sig, (size_t)w->sig_len, sig, sizeof(sig), &sig_n) != 0 ||
        psa_import_key(&attr, point, sizeof(point), &key) != PSA_SUCCESS) {
        return false;
    }
    psa_status_t st =
        psa_verify_message(key, PSA_ALG_ECDSA(PSA_ALG_SHA_256), msg, (size_t)w->ad_len + 32, sig, sig_n);
    psa_destroy_key(key);
    return st == PSA_SUCCESS;
}

static int passkey_by_id(const webauthn_t *w, const char *rp)
{
    for (int i = 0; i < PASSKEYS; i++) {
        if (passkeys[i].id_len && passkeys[i].id_len == w->id_len &&
            memcmp(passkeys[i].id, w->id, (size_t)w->id_len) == 0 && strcmp(passkeys[i].rp, rp) == 0) {
            return i;
        }
    }
    return -1;
}

esp_err_t auth_passkey_login(httpd_req_t *req)
{
    char rp[64], origin[80];
    if (passkey_refused(req, rp, origin) || locked(req)) {
        return ESP_OK;
    }
    webauthn_t *w = heap_caps_malloc(sizeof(*w), MALLOC_CAP_SPIRAM);
    db_authdata_t a = {0};
    bool ok = w && read_webauthn(req, w) && client_ok(req, w, true, origin) &&
              db_authdata(w->ad, (size_t)w->ad_len, &a) && rp_ok(&a, rp);
    int i = ok ? passkey_by_id(w, rp) : -1;
    ok = i >= 0 && (!passkeys[i].uv || (a.flags & DB_AD_UV)) && db_count_ok(passkeys[i].count, a.count) &&
         signed_by(&passkeys[i], w);
    heap_caps_free(w);
    count_try(req, ok, true);
    if (!ok) {
        return send_error(req, "403 Forbidden", "passkey not accepted");
    }
    if (a.count) {
        passkeys[i].count = a.count;
        save_passkeys(); /* not saved: a copied passkey could log in once more, nothing else */
    }
    ESP_LOGI(TAG, "login with passkey \"%s\"", passkeys[i].name);
    auth_session_cookie(req);
    return send_json(req, "{\"ok\":true}");
}

static bool name_ok(const char *name)
{
    for (const unsigned char *c = (const unsigned char *)name; *c; c++) {
        if (*c < 0x20 || *c == 0x7f) {
            return false;
        }
    }
    return name[0];
}

/* POST: a new passkey from navigator.credentials.create(), with its public key as SPKI. */
esp_err_t auth_passkey_add(httpd_req_t *req)
{
    char rp[64], origin[80];
    if (passkey_refused(req, rp, origin)) {
        return ESP_OK;
    }
    webauthn_t *w = heap_caps_malloc(sizeof(*w), MALLOC_CAP_SPIRAM);
    passkey_t pk = {0};
    db_authdata_t a = {0};
    bool read = w && read_webauthn(req, w);
    const char *bad = !read               ? "a WebAuthn registration expected"
                      : !name_ok(w->name) ? "name: 1 to 23 bytes"
                      : w->alg != -7 || w->spki_len < 0 || !db_spki_p256(w->spki, (size_t)w->spki_len, pk.xy)
                          ? "only ES256 (P-256) passkeys, with the public key from the browser"
                      : !client_ok(req, w, false, origin) || !db_authdata(w->ad, (size_t)w->ad_len, &a) ||
                              !rp_ok(&a, rp) || !a.cred || a.cred_len != (size_t)w->id_len ||
                              a.cred_len > sizeof(pk.id) || memcmp(a.cred, w->id, a.cred_len) != 0
                          ? "passkey not accepted"
                          : NULL;
    if (!bad) {
        memcpy(pk.id, w->id, a.cred_len);
        pk.id_len = (uint8_t)a.cred_len;
        pk.uv = a.flags & DB_AD_UV;
        pk.count = a.count;
        strlcpy(pk.rp, rp, sizeof(pk.rp));
        strlcpy(pk.name, w->name, sizeof(pk.name));
    }
    heap_caps_free(w);
    if (bad) {
        return send_error(req, "400 Bad Request", bad);
    }
    int slot = -1;
    for (int i = 0; i < PASSKEYS && slot < 0; i++) {
        slot = passkeys[i].id_len ? -1 : i;
    }
    if (slot < 0) {
        return send_error(req, "409 Conflict", "8 passkeys already, remove one first");
    }
    passkeys[slot] = pk;
    if (save_passkeys() != ESP_OK) {
        memset(&passkeys[slot], 0, sizeof(passkeys[slot]));
        return send_error(req, "500 Internal Server Error", "not saved");
    }
    char reply[24];
    snprintf(reply, sizeof(reply), "{\"id\":%d}", slot);
    return send_json(req, reply);
}

esp_err_t auth_passkeys_list(httpd_req_t *req)
{
    char buf[PASSKEYS * 150 + 16];
    db_json_t j;
    db_json_begin(&j, buf, sizeof(buf));
    db_json_arr_open(&j, NULL);
    for (int i = 0; i < PASSKEYS; i++) {
        if (passkeys[i].id_len) {
            db_json_obj_open(&j, NULL);
            db_json_int(&j, "id", i);
            db_json_str(&j, "name", passkeys[i].name);
            db_json_str(&j, "rp", passkeys[i].rp);
            db_json_obj_close(&j);
        }
    }
    db_json_arr_close(&j);
    db_json_end(&j);
    return send_json(req, buf);
}

/* PUT {"name"} renames, DELETE removes /api/passkeys/<id>. */
esp_err_t auth_passkey_edit(httpd_req_t *req)
{
    const char *s = strncmp(req->uri, "/api/passkeys/", 14) == 0 ? req->uri + 14 : "";
    int i = s[0] >= '0' && s[0] < '0' + PASSKEYS && !s[1] ? s[0] - '0' : -1;
    if (i < 0 || !passkeys[i].id_len) {
        return send_error(req, "404 Not Found", "no such passkey");
    }
    passkey_t before = passkeys[i];
    if (req->method == HTTP_DELETE) {
        memset(&passkeys[i], 0, sizeof(passkeys[i]));
    } else {
        char body[96];
        cJSON *root = read_body(req, body, sizeof(body)) < 0 ? NULL : cJSON_Parse(body);
        const cJSON *name = cJSON_GetObjectItemCaseSensitive(root, "name");
        bool ok = cJSON_IsString(name) && strlen(name->valuestring) < sizeof(before.name) &&
                  name_ok(name->valuestring);
        if (ok) {
            strlcpy(passkeys[i].name, name->valuestring, sizeof(passkeys[i].name));
        }
        cJSON_Delete(root);
        if (!ok) {
            return send_error(req, "400 Bad Request", "name: 1 to 23 bytes");
        }
    }
    if (save_passkeys() != ESP_OK) {
        passkeys[i] = before;
        return send_error(req, "500 Internal Server Error", "not saved");
    }
    return send_json(req, "{\"ok\":true}");
}

static esp_err_t save_tokens(void)
{
    nvs_handle_t h;
    esp_err_t err = nvs_open("auth", NVS_READWRITE, &h);
    if (err == ESP_OK) {
        err = nvs_set_blob(h, "tokens", tokens, sizeof(tokens));
        err = err == ESP_OK ? nvs_set_u8(h, "rights", full_rights) : err;
        err = err == ESP_OK ? nvs_commit(h) : err;
        nvs_close(h);
    }
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "tokens not saved: %s", esp_err_to_name(err));
    }
    return err;
}

static void sessions_sum(const uint8_t pass[32], uint8_t out[32])
{
    psa_hash_operation_t op = PSA_HASH_OPERATION_INIT;
    size_t len;
    psa_crypto_init();
    psa_hash_setup(&op, PSA_ALG_SHA_256);
    psa_hash_update(&op, (const uint8_t *)sessions, sizeof(sessions));
    psa_hash_update(&op, pass, 32);
    psa_hash_finish(&op, out, 32, &len);
}

static void keep_sessions(void)
{
    int64_t now = esp_timer_get_time();
    portENTER_CRITICAL(&mux);
    for (int i = 0; i < SESSIONS; i++) {
        sessions[i].last_used = now - sessions[i].last_used;
    }
    portEXIT_CRITICAL(&mux);
    sessions_sum(pass_sha, kept_sum);
}

static void restore_sessions(void)
{
    sha256(db_config()->admin_pass, pass_sha);
    uint8_t sum[32];
    sessions_sum(pass_sha, sum);
    int64_t now = esp_timer_get_time();
    int kept = 0;
    if (memcmp(sum, kept_sum, sizeof(sum)) == 0) {
        for (int i = 0; i < SESSIONS; i++) {
            sessions[i].last_used = now - sessions[i].last_used;
            kept += sessions[i].token[0] != 0;
        }
    } else {
        memset(sessions, 0, sizeof(sessions));
    }
    memset(kept_sum, 0, sizeof(kept_sum)); /* a panic or a reset pin later must not bring them back */
    ESP_LOGI(TAG, "%d web sessions kept over the restart", kept);
    if (esp_register_shutdown_handler(keep_sessions) != ESP_OK) {
        ESP_LOGE(TAG, "no shutdown handler left: a restart ends the web sessions");
    }
}

void auth_start(void)
{
    started = esp_timer_get_time();
    restore_sessions();
    nvs_handle_t h;
    size_t len = sizeof(tokens);
    esp_err_t err = nvs_open("auth", NVS_READONLY, &h);
    if (err == ESP_OK) {
        err = nvs_get_blob(h, "tokens", tokens, &len);
        nvs_get_u8(h, "rights", &full_rights); /* missing: every older token stays control-only */
        nvs_close(h);
    }
    if (err != ESP_OK && err != ESP_ERR_NVS_NOT_FOUND) {
        ESP_LOGE(TAG, "API tokens not read: %s", esp_err_to_name(err));
        memset(tokens, 0, sizeof(tokens));
        tokens_unread = true;
    }
    load_passkeys();
}

static bool tokens_refused(httpd_req_t *req)
{
    if (tokens_unread) {
        send_error(req, "500 Internal Server Error", "the stored tokens could not be read at this start");
    }
    return tokens_unread;
}

esp_err_t auth_tokens_list(httpd_req_t *req)
{
    if (tokens_refused(req)) {
        return ESP_OK;
    }
    char buf[API_TOKENS * 60 + 16];
    db_json_t j;
    db_json_begin(&j, buf, sizeof(buf));
    db_json_arr_open(&j, NULL);
    for (int i = 0; i < API_TOKENS; i++) {
        if (tokens[i].name[0]) {
            db_json_obj_open(&j, NULL);
            db_json_str(&j, "name", tokens[i].name);
            db_json_str(&j, "right", full_rights >> i & 1 ? "full" : "control");
            db_json_obj_close(&j);
        }
    }
    db_json_arr_close(&j);
    db_json_end(&j);
    return send_json(req, buf);
}

/* The token is in the reply once and never again: only its SHA-256 stays. */
esp_err_t auth_tokens_add(httpd_req_t *req)
{
    if (tokens_refused(req)) {
        return ESP_OK;
    }
    char body[96];
    cJSON *root = read_body(req, body, sizeof(body)) < 0 ? NULL : cJSON_Parse(body);
    const cJSON *name = cJSON_GetObjectItemCaseSensitive(root, "name");
    const cJSON *right = cJSON_GetObjectItemCaseSensitive(root, "right");
    bool full = cJSON_IsString(right) && strcmp(right->valuestring, "full") == 0;
    bool right_ok = !right || (cJSON_IsString(right) && (full || strcmp(right->valuestring, "control") == 0));
    bool ok =
        cJSON_IsString(name) && name->valuestring[0] && strlen(name->valuestring) < sizeof(tokens[0].name);
    for (const char *c = ok ? name->valuestring : ""; *c; c++) {
        ok &= isalnum((unsigned char)*c) || *c == '-' || *c == '_'; /* it goes into a URL to remove it */
    }
    char wanted[sizeof(tokens[0].name)] = {0};
    if (ok) {
        strlcpy(wanted, name->valuestring, sizeof(wanted));
    }
    cJSON_Delete(root);
    if (!ok) {
        return send_error(req, "400 Bad Request", "name: 1 to 23 letters, digits, - or _");
    }
    if (!right_ok) {
        return send_error(req, "400 Bad Request", "right: control or full");
    }
    char token[TOKEN_BYTES * 2 + 1];
    uint8_t raw[TOKEN_BYTES];
    esp_fill_random(raw, sizeof(raw));
    db_hex(token, raw, sizeof(raw));
    api_token_t t = {0};
    strlcpy(t.name, wanted, sizeof(t.name));
    sha256(token, t.sha);
    int slot = -1;
    api_token_t before = {0};
    uint8_t rights_before = full_rights;
    portENTER_CRITICAL(&mux);
    for (int i = 0; i < API_TOKENS; i++) {
        if (strcmp(tokens[i].name, wanted) == 0 || (slot < 0 && !tokens[i].name[0])) {
            slot = i; /* the same name replaces its token */
        }
    }
    if (slot >= 0) {
        before = tokens[slot];
        tokens[slot] = t;
        full_rights = (uint8_t)((full_rights & ~(1u << slot)) | (unsigned)full << slot);
    }
    portEXIT_CRITICAL(&mux);
    if (slot < 0) {
        return send_error(req, "409 Conflict", "8 tokens already, remove one first");
    }
    if (save_tokens() != ESP_OK) {
        portENTER_CRITICAL(&mux);
        tokens[slot] = before; /* the old token keeps working, the new one was never shown */
        full_rights = rights_before;
        portEXIT_CRITICAL(&mux);
        return send_error(req, "500 Internal Server Error",
                          "not saved: the token would be gone after a restart");
    }
    char reply[180];
    snprintf(reply, sizeof(reply), "{\"name\":\"%s\",\"right\":\"%s\",\"token\":\"%s\"}", wanted,
             full ? "full" : "control", token);
    return send_json(req, reply);
}

esp_err_t auth_tokens_remove(httpd_req_t *req)
{
    if (tokens_refused(req)) {
        return ESP_OK;
    }
    const char *name = req->uri + strlen("/api/tokens/");
    bool found = false;
    portENTER_CRITICAL(&mux);
    for (int i = 0; i < API_TOKENS; i++) {
        if (tokens[i].name[0] && strcmp(tokens[i].name, name) == 0) {
            memset(&tokens[i], 0, sizeof(tokens[i]));
            full_rights &= (uint8_t)~(1u << i);
            found = true;
        }
    }
    portEXIT_CRITICAL(&mux);
    if (!found) {
        return send_error(req, "404 Not Found", "no such token");
    }
    if (save_tokens() != ESP_OK) { /* gone until the next restart, then it works again */
        return send_error(req, "500 Internal Server Error",
                          "not saved: the token works again after a restart");
    }
    return send_json(req, "{\"ok\":true}");
}
