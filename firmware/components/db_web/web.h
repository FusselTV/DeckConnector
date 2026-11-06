#pragma once

#include <stdbool.h>
#include <stddef.h>

#include "esp_err.h"
#include "esp_http_server.h"

/* Replies and request bodies, shared by the handlers in this component. */
esp_err_t send_json(httpd_req_t *req, const char *json);
esp_err_t send_error(httpd_req_t *req, const char *status, const char *msg);
/* The whole body, NUL-terminated; -1 when it is cap or longer, or the client stops sending. */
int read_body(httpd_req_t *req, char *buf, size_t cap);
/* One handler at a time across both servers, see timed(). */
void web_lock(void);
void web_unlock(void);
/* Restart in a second, so the reply still goes out. */
void restart_soon(void);
/* The HTTPS certificate and key as PEM, made and kept on the first start; its SHA-256 fingerprint. */
bool web_cert(const char *name, char **cert, char **key, char fingerprint[96]);
/* The request came over HTTPS. */
bool web_tls(httpd_req_t *req);
/* Plain HTTP while "nur HTTPS" is on: it gets no answer but a redirect. */
bool web_plain_refused(httpd_req_t *req);
