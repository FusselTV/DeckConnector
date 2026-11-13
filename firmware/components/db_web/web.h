#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"
#include "esp_http_server.h"
#include "sdkconfig.h"

/* Sockets: lwIP on the chip, the host's own in the simulator (firmware/sim). */
#if CONFIG_IDF_TARGET_LINUX
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <sys/select.h>
#include <sys/socket.h>
#else
#include "lwip/sockets.h"
#endif

#define STATUS_JSON_MAX 8192 /* 2.4 KB in use; the bench reached 4 KB with test servers and pairings */

/* Who may call a route: anyone; any token or the password (cookie or basic auth); a full-rights token or the
 * password; the password only. */
typedef enum { WEB_OPEN, WEB_CONTROL, WEB_FULL, WEB_ADMIN } web_level_t;

/* Every route but the web socket runs through timed(): the login check, and the time for /api/profile. Ports
 * 80 and 443 are two server tasks, and the handlers share static buffers, so timed() runs one handler at a
 * time, except the firmware upload, which shares nothing and may take a minute. */
typedef struct {
    esp_err_t (*handler)(httpd_req_t *req);
    web_level_t level;
    bool deck; /* acts on a deck: checked by web_deck_refused() */
    uint32_t count;
    uint32_t max_us;
    uint64_t total_us;
} route_t;
#define WEB_ROUTES_MAX 60
extern httpd_uri_t web_uris[WEB_ROUTES_MAX];
extern route_t web_routes[WEB_ROUTES_MAX];
extern size_t web_route_count;

/* `?deck=<serial>` names no attached deck, or a token asks while the deck shows a login: the reply is sent.
 */
bool web_deck_refused(httpd_req_t *req);

/* Replies and request bodies, shared by the handlers in this component. */
esp_err_t send_json(httpd_req_t *req, const char *json);
esp_err_t send_error(httpd_req_t *req, const char *status, const char *msg);
/* The whole body, NUL-terminated; -1 when it is cap or longer, or the client stops sending. */
int read_body(httpd_req_t *req, char *buf, size_t cap);
/* One handler at a time across both servers, see timed(). */
bool web_lock_for(uint32_t ms); /* false: another handler held it that long */
void web_unlock(void);
/* A reply built into a heap buffer; build returns 0 when it does not fit. */
esp_err_t send_built(httpd_req_t *req, size_t cap, size_t (*build)(char *buf, size_t cap));
/* CPU, RAM and PSRAM sampled every 5 s into RAM, and the time spent per route (metrics.c). */
void metrics_start(void);
esp_err_t get_metrics(httpd_req_t *req);
esp_err_t get_profile(httpd_req_t *req);
/* Restart in a second, so the reply still goes out. */
void restart_soon(void);
/* The HTTPS certificate and key as PEM, made and kept on the first start; its SHA-256 fingerprint. */
bool web_cert(const char *name, char **cert, char **key, char fingerprint[96]);
/* An uploaded certificate (PEM, its chain may follow) and key: they parse and belong together. Fills `info`
 * with {ok, subject, issuer, not_after, sha256} when cap > 0. */
bool web_cert_check(const char *cert, const char *key, char *info, size_t cap);
/* Keep an uploaded pair for the next start, or drop it (cert NULL) and go back to the own certificate. */
esp_err_t web_cert_store(const char *cert, const char *key);
bool web_cert_custom(void);
bool web_cert_custom_unread(void);
esp_err_t get_cert(httpd_req_t *req);
esp_err_t put_tls(httpd_req_t *req);
esp_err_t delete_tls(httpd_req_t *req);
bool web_tls(httpd_req_t *req);
/* Plain HTTP while "nur HTTPS" is on: it gets no answer but a redirect. */
bool web_plain_refused(httpd_req_t *req);
bool web_tls_running(void);
