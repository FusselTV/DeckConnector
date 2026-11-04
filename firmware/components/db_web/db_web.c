#include "db_web.h"

#include <stdio.h>
#include <string.h>

#include "db_config.h"
#include "db_deck.h"
#include "db_log.h"
#include "db_net.h"
#include "db_ota.h"
#include "esp_heap_caps.h"
#include "esp_http_server.h"
#include "esp_log.h"
#include "esp_system.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "lwip/sockets.h"
#include "mbedtls/base64.h"

#define BODY_MAX 2048
#define CHUNK    4096
#define JSON_MAX 2048

static const char *TAG = "web";

extern const char index_html_start[] asm("_binary_index_html_start");
extern const char index_html_end[] asm("_binary_index_html_end");

static size_t (*status_json)(char *buf, size_t cap);

static bool authorized(httpd_req_t *req)
{
    char given[96] = {0};
    char plain[64];
    unsigned char expected[96];
    size_t n = 0;
    if (httpd_req_get_hdr_value_str(req, "Authorization", given, sizeof(given)) == ESP_OK) {
        int len = snprintf(plain, sizeof(plain), "admin:%s", db_config()->admin_pass);
        mbedtls_base64_encode(expected, sizeof(expected) - 1, &n, (const unsigned char *)plain, (size_t)len);
        expected[n] = '\0';
        if (strncmp(given, "Basic ", 6) == 0 && strcmp(given + 6, (const char *)expected) == 0) {
            return true;
        }
    }
    httpd_resp_set_status(req, "401 Unauthorized");
    httpd_resp_set_hdr(req, "WWW-Authenticate", "Basic realm=\"deckbridge\"");
    httpd_resp_sendstr(req, "login required");
    return false;
}

static esp_err_t send_json(httpd_req_t *req, const char *json, size_t len)
{
    httpd_resp_set_type(req, "application/json");
    httpd_resp_set_hdr(req, "Cache-Control", "no-store");
    return httpd_resp_send(req, json, (ssize_t)len);
}

static esp_err_t send_error(httpd_req_t *req, const char *status, const char *msg)
{
    char buf[200];
    int n = snprintf(buf, sizeof(buf), "{\"ok\":false,\"error\":\"%s\"}", msg);
    httpd_resp_set_status(req, status);
    return send_json(req, buf, (size_t)n);
}

static int read_body(httpd_req_t *req, char *buf, size_t cap)
{
    if (req->content_len >= cap) {
        return -1;
    }
    size_t got = 0;
    while (got < req->content_len) {
        int n = httpd_req_recv(req, buf + got, req->content_len - got);
        if (n <= 0) {
            return -1;
        }
        got += (size_t)n;
    }
    buf[got] = '\0';
    return (int)got;
}

static void restart_task(void *arg)
{
    vTaskDelay(pdMS_TO_TICKS(1000));
    esp_restart();
}

static esp_err_t page(httpd_req_t *req)
{
    if (!authorized(req)) {
        return ESP_OK;
    }
    httpd_resp_set_type(req, "text/html; charset=utf-8");
    return httpd_resp_send(req, index_html_start,
                           index_html_end - index_html_start - 1); /* EMBED_TXTFILES adds a NUL */
}

static esp_err_t get_status(httpd_req_t *req)
{
    if (!authorized(req)) {
        return ESP_OK;
    }
    char *buf = malloc(JSON_MAX);
    if (!buf) {
        return send_error(req, "500 Internal Server Error", "memory");
    }
    esp_err_t err = send_json(req, buf, status_json(buf, JSON_MAX));
    free(buf);
    return err;
}

static esp_err_t get_config(httpd_req_t *req)
{
    if (!authorized(req)) {
        return ESP_OK;
    }
    char buf[JSON_MAX];
    return send_json(req, buf, db_config_to_json(db_config(), false, buf, sizeof(buf)));
}

/* Brightness changes apply at once; everything else needs a restart to take effect cleanly. */
static esp_err_t put_config(httpd_req_t *req)
{
    if (!authorized(req)) {
        return ESP_OK;
    }
    char body[BODY_MAX];
    if (read_body(req, body, sizeof(body)) < 0) {
        return send_error(req, "413 Payload Too Large", "body too large");
    }
    db_config_t old;
    db_config_t cfg;
    db_config_copy(&old);
    cfg = old;
    char err[96];
    if (db_config_from_json(&cfg, body, err, sizeof(err)) != ESP_OK) {
        return send_error(req, "400 Bad Request", err);
    }
    if (db_config_save(&cfg) != ESP_OK) {
        return send_error(req, "500 Internal Server Error", "saving failed");
    }
    db_config_t without_brightness = cfg;
    without_brightness.brightness = old.brightness;
    bool restart = memcmp(&without_brightness, &old, sizeof(old)) != 0;
    if (cfg.brightness != old.brightness) {
        db_deck_set_brightness(cfg.brightness);
    }
    const char *reply = restart ? "{\"ok\":true,\"restart\":true}" : "{\"ok\":true,\"restart\":false}";
    send_json(req, reply, strlen(reply));
    if (restart) {
        ESP_LOGW(TAG, "config changed, restarting");
        xTaskCreate(restart_task, "web_restart", 2048, NULL, 5, NULL);
    }
    return ESP_OK;
}

static esp_err_t post_ota(httpd_req_t *req)
{
    if (!authorized(req)) {
        return ESP_OK;
    }
    if (db_ota_begin(req->content_len) != ESP_OK) {
        return send_error(req, "409 Conflict", "an update is already running or the file is empty");
    }
    uint8_t *buf = heap_caps_malloc(CHUNK, MALLOC_CAP_INTERNAL);
    size_t left = req->content_len;
    esp_err_t err = buf ? ESP_OK : ESP_ERR_NO_MEM;
    int timeouts = 0;
    while (err == ESP_OK && left > 0) {
        int n = httpd_req_recv(req, (char *)buf, left < CHUNK ? left : CHUNK);
        if (n == HTTPD_SOCK_ERR_TIMEOUT && ++timeouts < 3) {
            continue; /* a vanished client must not hold the update lock forever */
        }
        if (n <= 0) {
            err = ESP_FAIL;
            db_ota_abort("upload interrupted");
            break;
        }
        err = db_ota_write(buf, (size_t)n);
        left -= (size_t)n;
    }
    heap_caps_free(buf);
    if (err == ESP_OK) {
        err = db_ota_end();
    }
    if (err != ESP_OK) {
        db_ota_abort(esp_err_to_name(err));
        return send_error(req, "400 Bad Request", esp_err_to_name(err));
    }
    return send_json(req, "{\"ok\":true,\"restart\":true}", 26);
}

static esp_err_t post_reboot(httpd_req_t *req)
{
    if (!authorized(req)) {
        return ESP_OK;
    }
    send_json(req, "{\"ok\":true}", 11);
    xTaskCreate(restart_task, "web_restart", 2048, NULL, 5, NULL);
    return ESP_OK;
}

static esp_err_t get_log(httpd_req_t *req)
{
    if (!authorized(req)) {
        return ESP_OK;
    }
    char *buf = heap_caps_malloc(16 * 1024, MALLOC_CAP_SPIRAM);
    if (!buf) {
        return send_error(req, "500 Internal Server Error", "memory");
    }
    size_t n = db_log_read(buf, 16 * 1024);
    httpd_resp_set_type(req, "text/plain; charset=utf-8");
    esp_err_t err = httpd_resp_send(req, buf, (ssize_t)n);
    heap_caps_free(buf);
    return err;
}

/* Phones probe some URL when they join a WLAN; sending them to the page opens it by itself. */
static esp_err_t not_found(httpd_req_t *req, httpd_err_code_t code)
{
    db_net_status_t n;
    db_net_status(&n);
    if (!n.ap_active) {
        httpd_resp_send_err(req, HTTPD_404_NOT_FOUND, "not found");
        return ESP_FAIL;
    }
    char loc[40];
    snprintf(loc, sizeof(loc), "http://%s/", n.ap_ip);
    httpd_resp_set_status(req, "302 Found");
    httpd_resp_set_hdr(req, "Location", loc);
    return httpd_resp_send(req, NULL, 0);
}

/* Answer every A query with the address of the setup AP while it is on. */
static void dns_task(void *arg)
{
    int sock = socket(AF_INET, SOCK_DGRAM, 0);
    struct sockaddr_in addr = {.sin_family = AF_INET, .sin_port = htons(53)};
    if (sock < 0 || bind(sock, (struct sockaddr *)&addr, sizeof(addr)) != 0) {
        ESP_LOGE(TAG, "captive DNS not available");
        vTaskDelete(NULL);
    }
    uint8_t pkt[512];
    for (;;) {
        struct sockaddr_in from;
        socklen_t from_len = sizeof(from);
        int n = recvfrom(sock, pkt, sizeof(pkt) - 16, 0, (struct sockaddr *)&from, &from_len);
        db_net_status_t st;
        db_net_status(&st);
        if (n < 12 || !st.ap_active) {
            continue;
        }
        /* Keep only header and question: an EDNS record after the question would hide our answer. */
        int q = 12;
        while (q < n && pkt[q] != 0) {
            q += pkt[q] + 1;
        }
        q += 5; /* zero label, type, class */
        if (q > n) {
            continue;
        }
        n = q;
        memset(pkt + 8, 0, 4); /* no authority or additional records */
        struct in_addr ip;
        inet_aton(st.ap_ip, &ip);
        pkt[2] = 0x81; /* response, recursion desired */
        pkt[3] = 0x80; /* recursion available, no error */
        pkt[7] = 1;    /* one answer */
        const uint8_t answer[] = {0xc0, 0x0c, 0, 1, 0, 1, 0, 0, 0, 60, 0, 4};
        memcpy(pkt + n, answer, sizeof(answer));
        memcpy(pkt + n + sizeof(answer), &ip.s_addr, 4);
        sendto(sock, pkt, (size_t)n + sizeof(answer) + 4, 0, (struct sockaddr *)&from, from_len);
    }
}

esp_err_t db_web_start(size_t (*status)(char *buf, size_t cap))
{
    status_json = status;
    httpd_config_t cfg = HTTPD_DEFAULT_CONFIG();
    cfg.stack_size = 8192;
    cfg.max_uri_handlers = 10;
    cfg.lru_purge_enable = true;
    cfg.recv_wait_timeout = 15;
    httpd_handle_t server;
    esp_err_t err = httpd_start(&server, &cfg);
    if (err != ESP_OK) {
        return err;
    }
    const httpd_uri_t uris[] = {
        {.uri = "/", .method = HTTP_GET, .handler = page},
        {.uri = "/api/status", .method = HTTP_GET, .handler = get_status},
        {.uri = "/api/config", .method = HTTP_GET, .handler = get_config},
        {.uri = "/api/config", .method = HTTP_PUT, .handler = put_config},
        {.uri = "/api/ota", .method = HTTP_POST, .handler = post_ota},
        {.uri = "/api/reboot", .method = HTTP_POST, .handler = post_reboot},
        {.uri = "/api/log", .method = HTTP_GET, .handler = get_log},
    };
    for (size_t i = 0; i < sizeof(uris) / sizeof(uris[0]); i++) {
        httpd_register_uri_handler(server, &uris[i]);
    }
    httpd_register_err_handler(server, HTTPD_404_NOT_FOUND, not_found);
    xTaskCreate(dns_task, "dns", 3072, NULL, 3, NULL);
    ESP_LOGI(TAG, "web UI on port 80");
    return ESP_OK;
}
