#pragma once

#include <stddef.h>

#include "esp_http_server.h"

/*
 * What the HTTP handlers and the web socket share. Each takes a JSON body, writes the JSON reply and returns
 * the HTTP status. Not reentrant: the web server runs one handler at a time.
 */
int api_config(const char *body, char *reply, size_t cap);
int api_config_token(const char *body, char *reply, size_t cap);
int api_input(const char *body, char *reply, size_t cap);

/* The HTTP handlers of the API; http.c routes to them. */
extern size_t (*api_status_json)(char *buf, size_t cap);
esp_err_t get_status(httpd_req_t *req);
esp_err_t get_config(httpd_req_t *req);
esp_err_t put_config(httpd_req_t *req);
esp_err_t post_input(httpd_req_t *req);
esp_err_t post_menu(httpd_req_t *req);
esp_err_t delete_menu(httpd_req_t *req);
esp_err_t post_dock(httpd_req_t *req);
esp_err_t get_backup(httpd_req_t *req);
esp_err_t get_events(httpd_req_t *req);
esp_err_t delete_denied(httpd_req_t *req);
esp_err_t post_deck_reconnect(httpd_req_t *req);
esp_err_t post_ota(httpd_req_t *req);
esp_err_t get_slots(httpd_req_t *req);
esp_err_t get_frontend(httpd_req_t *req);
esp_err_t get_companions(httpd_req_t *req);
esp_err_t get_wifi_scan(httpd_req_t *req);
esp_err_t post_source_retry(httpd_req_t *req);
esp_err_t get_input_log(httpd_req_t *req);
esp_err_t get_update_check(httpd_req_t *req);
esp_err_t put_setup(httpd_req_t *req);
esp_err_t post_ota_inspect(httpd_req_t *req);
esp_err_t post_source_probe(httpd_req_t *req);
esp_err_t post_wifi_join(httpd_req_t *req);
esp_err_t put_frontend(httpd_req_t *req);
esp_err_t delete_frontend(httpd_req_t *req);
esp_err_t get_coredump(httpd_req_t *req);
esp_err_t get_boot(httpd_req_t *req);
esp_err_t delete_coredump(httpd_req_t *req);
esp_err_t post_debug_crash(httpd_req_t *req);
esp_err_t post_slot_boot(httpd_req_t *req);
esp_err_t post_reboot(httpd_req_t *req);
esp_err_t get_log(httpd_req_t *req);
esp_err_t get_screen(httpd_req_t *req);
esp_err_t get_screen_image(httpd_req_t *req);
esp_err_t get_screen_jpg(httpd_req_t *req);
esp_err_t post_ota_url(httpd_req_t *req);
