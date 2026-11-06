#pragma once

#include <stdbool.h>
#include <stddef.h>

#include "esp_err.h"
#include "esp_http_server.h"

/* Every route but the web socket runs through timed(): the login check, and the time for /api/profile. Ports
 * 80 and 443 are two server tasks, and the handlers share static buffers, so timed() runs one handler at a
 * time. */
typedef struct {
    esp_err_t (*handler)(httpd_req_t *req);
    bool open; /* no login needed */
    uint32_t count;
    uint32_t max_us;
    uint64_t total_us;
} route_t;
extern httpd_uri_t web_uris[24];
extern route_t web_routes[24];
extern size_t web_route_count;

/* Replies and request bodies, shared by the handlers in this component. */
esp_err_t send_json(httpd_req_t *req, const char *json);
esp_err_t send_error(httpd_req_t *req, const char *status, const char *msg);
/* The whole body, NUL-terminated; -1 when it is cap or longer, or the client stops sending. */
int read_body(httpd_req_t *req, char *buf, size_t cap);
/* One handler at a time across both servers, see timed(). */
void web_lock(void);
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
/* The request came over HTTPS. */
bool web_tls(httpd_req_t *req);
/* Plain HTTP while "nur HTTPS" is on: it gets no answer but a redirect. */
bool web_plain_refused(httpd_req_t *req);
