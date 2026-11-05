#pragma once

#include <stddef.h>

#include "esp_err.h"
#include "esp_http_server.h"

/* Replies and request bodies, shared by the handlers in this component. */
esp_err_t send_json(httpd_req_t *req, const char *json);
esp_err_t send_error(httpd_req_t *req, const char *status, const char *msg);
/* The whole body, NUL-terminated; -1 when it is cap or longer, or the client stops sending. */
int read_body(httpd_req_t *req, char *buf, size_t cap);
/* Restart in a second, so the reply still goes out. */
void restart_soon(void);
