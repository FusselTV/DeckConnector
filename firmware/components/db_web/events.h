#pragma once

#include <stddef.h>
#include <stdint.h>

#include "esp_http_server.h"

/*
 * /api/ws: a web socket that sends {"t":"state"} on connect and on every change, {"t":"screen"} when a deck's
 * picture changes and {"t":"input"} for every press, turn and touch, both with the deck's serial. It takes
 * {"t":"set", ...config} and {"t":"input", ...} (with "deck": a serial, else the first deck) and answers each
 * with {"t":"result"}. {"t":"event","event":{...}} brings each new event as GET /api/events has it.
 */
esp_err_t events_check(httpd_req_t *req);
esp_err_t events_open(httpd_req_t *req);
esp_err_t events_handler(httpd_req_t *req);
esp_err_t events_start(size_t (*state_json)(char *buf, size_t cap),
                       size_t (*screen_json)(int deck, char *buf, size_t cap));
#define INPUT_LOG  2000
#define INPUT_PAGE 400
/* The newest INPUT_PAGE inputs after `since` and before `before` (seq numbers, 0: no limit), oldest first, as a
 * JSON array; 0 when it does not fit. */
size_t events_input_log(char *buf, size_t cap, uint32_t since, uint32_t before);
