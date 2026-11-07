#pragma once

#include <stddef.h>
#include <stdint.h>

/* The input log after `since` (a seq number, 0 for all kept) as a JSON array; 0 when it does not fit. */
size_t events_input_log(char *buf, size_t cap, uint32_t since);

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
