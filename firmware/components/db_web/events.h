#pragma once

#include <stddef.h>

#include "esp_http_server.h"

/*
 * /api/ws: a web socket that sends {"t":"state"} on connect and on every change, {"t":"screen"} when the
 * picture changes and {"t":"input"} for every press, turn and touch. It takes {"t":"set", ...config} and
 * {"t":"input", ...} and answers each with {"t":"result"}.
 */
esp_err_t events_check(httpd_req_t *req);
esp_err_t events_open(httpd_req_t *req);
esp_err_t events_handler(httpd_req_t *req);
void events_start(size_t (*state_json)(char *buf, size_t cap));
