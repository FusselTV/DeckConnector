#include "events.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "api.h"
#include "auth.h"
#include "cJSON.h"
#include "db_deck.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "lwip/sockets.h"

#define CLIENTS   8
#define STATE_MAX 2048
#define POLL_MS   200
#define FRAME_MAX 512
#define INPUT_MAX 160
#define PING_MS                                                                                              \
    5000 /* a listener hears something this often, so it can tell a dead link from a quiet deck              \
          */

static const char *TAG = "events";

static struct {
    httpd_handle_t server;
    size_t (*state_json)(char *buf, size_t cap);
    SemaphoreHandle_t lock; /* the client list, and one frame on the wire at a time */
    QueueHandle_t inputs;   /* presses go out from our task: a stuck client must not stall the deck */
    int fds[CLIENTS];
    char *state; /* the last state sent, to send only changes */
    uint32_t screen;
    unsigned evict;
} e;

/* A listener that stopped reading fills its socket, and the next send would block the sender for the send
 * timeout while it holds the lock the web server waits for. Such a listener is closed instead. */
static bool writable(int fd)
{
    fd_set w;
    FD_ZERO(&w);
    FD_SET(fd, &w);
    struct timeval now = {0};
    return select(fd + 1, NULL, &w, NULL, &now) > 0;
}

static void send_to(int fd, const char *json)
{
    if (!writable(fd)) {
        ESP_LOGW(TAG, "listener %d does not read, closing it", fd);
        httpd_sess_trigger_close(e.server, fd);
        return;
    }
    httpd_ws_frame_t f = {.type = HTTPD_WS_TYPE_TEXT, .payload = (uint8_t *)json, .len = strlen(json)};
    httpd_ws_send_frame_async(e.server, fd, &f);
}

static void send_locked(int fd, const char *json)
{
    xSemaphoreTake(e.lock, portMAX_DELAY);
    send_to(fd, json);
    xSemaphoreGive(e.lock);
}

static void broadcast(const char *json)
{
    xSemaphoreTake(e.lock, portMAX_DELAY);
    for (int i = 0; i < CLIENTS; i++) {
        int fd = e.fds[i];
        if (fd < 0) {
            continue;
        }
        if (httpd_ws_get_fd_info(e.server, fd) != HTTPD_WS_CLIENT_WEBSOCKET) {
            e.fds[i] = -1; /* gone */
            continue;
        }
        send_to(fd, json);
    }
    xSemaphoreGive(e.lock);
}

static bool anyone(void)
{
    for (int i = 0; i < CLIENTS; i++) {
        if (e.fds[i] >= 0) {
            return true;
        }
    }
    return false;
}

static const char *const kinds[] = {"",           "key_down",     "key_up",    "encoder_down",
                                    "encoder_up", "encoder_turn", "touch_tap", "touch_long",
                                    "touch_swipe"};

static void on_input(const db_input_t *ev)
{
    if (!anyone() || ev->kind >= sizeof(kinds) / sizeof(kinds[0])) {
        return;
    }
    char json[INPUT_MAX];
    snprintf(
        json, sizeof(json),
        "{\"t\":\"input\",\"kind\":\"%s\",\"index\":%u,\"value\":%d,\"x\":%u,\"y\":%u,\"x2\":%u,\"y2\":%u,"
        "\"writer\":\"%s\"}",
        kinds[ev->kind], ev->index, ev->value, ev->x, ev->y, ev->x2, ev->y2,
        db_deck_source_name(db_deck_writer()));
    xQueueSend(e.inputs, json, 0); /* full: the press is dropped for listeners, never for the deck */
}

/* State and screen changes, checked a few times a second while someone listens. */
static void poll_task(void *arg)
{
    char *buf = malloc(STATE_MAX);
    static db_shot_t shots[DB_SCREEN_MAX];
    char input[INPUT_MAX];
    TickType_t next = xTaskGetTickCount();
    TickType_t ping = next;
    for (;;) {
        if (xQueueReceive(e.inputs, input, pdMS_TO_TICKS(POLL_MS)) == pdTRUE) {
            broadcast(input);
        }
        if (xTaskGetTickCount() - ping >= pdMS_TO_TICKS(PING_MS) && anyone()) {
            ping = xTaskGetTickCount();
            broadcast("{\"t\":\"ping\"}");
        }
        if (xTaskGetTickCount() < next || !anyone() || !buf) {
            continue;
        }
        next = xTaskGetTickCount() + pdMS_TO_TICKS(POLL_MS);
        size_t n = e.state_json(buf, STATE_MAX);
        if (n && strcmp(buf, e.state) != 0) {
            memcpy(e.state, buf, n + 1);
            broadcast(buf);
        }
        db_source_t writer;
        size_t count = db_deck_shots(&writer, shots, DB_SCREEN_MAX);
        uint32_t key = db_deck_shots_key(writer, shots, count);
        if (key != e.screen) {
            e.screen = key;
            char json[96];
            snprintf(json, sizeof(json), "{\"t\":\"screen\",\"writer\":\"%s\",\"images\":%u}",
                     db_deck_source_name(writer), (unsigned)count);
            broadcast(json);
        }
    }
}

/* A page on another site may open a socket with the browser's login; only our own page may. */
static bool same_origin(httpd_req_t *req)
{
    char origin[80] = {0};
    char host[64] = {0};
    if (httpd_req_get_hdr_value_str(req, "Origin", origin, sizeof(origin)) != ESP_OK) {
        return true; /* not a browser: scripts and Home Assistant */
    }
    httpd_req_get_hdr_value_str(req, "Host", host, sizeof(host));
    const char *o = strstr(origin, "://");
    return o && strcmp(o + 3, host) == 0;
}

static void command(int fd, const char *text)
{
    cJSON *root = cJSON_Parse(text);
    const cJSON *t = cJSON_GetObjectItemCaseSensitive(root, "t");
    const cJSON *id_item = cJSON_GetObjectItemCaseSensitive(root, "id");
    int id = cJSON_IsNumber(id_item) ? id_item->valueint : 0; /* read before the item is deleted below */
    char reply[160] = "{\"ok\":false,\"error\":\"t: set or input expected\"}";
    int code = 400;
    if (cJSON_IsString(t) && (strcmp(t->valuestring, "set") == 0 || strcmp(t->valuestring, "input") == 0)) {
        bool set = t->valuestring[0] == 's';
        cJSON_DeleteItemFromObjectCaseSensitive(root, "t");
        cJSON_DeleteItemFromObjectCaseSensitive(root, "id");
        char *body = cJSON_PrintUnformatted(root);
        code = body ? (set ? api_config(body, reply, sizeof(reply)) : api_input(body, reply, sizeof(reply)))
                    : 500;
        free(body);
    }
    char out[224];
    snprintf(out, sizeof(out), "{\"t\":\"result\",\"id\":%d,\"status\":%d,\"reply\":%s}", id, code, reply);
    cJSON_Delete(root);
    send_locked(fd, out);
}

/* Before the upgrade, so a refused socket gets a plain 401 or 403 and never opens. */
esp_err_t events_check(httpd_req_t *req)
{
    if (!same_origin(req)) {
        httpd_resp_send_err(req, HTTPD_403_FORBIDDEN, "other origin");
        return ESP_FAIL;
    }
    return auth_check(req) ? ESP_OK : ESP_FAIL;
}

/* After the upgrade: the new listener gets the current state at once, then only changes. */
esp_err_t events_open(httpd_req_t *req)
{
    int fd = httpd_req_to_sockfd(req);
    int slot = -1;
    xSemaphoreTake(e.lock, portMAX_DELAY);
    for (int i = 0; i < CLIENTS; i++) {
        /* a closed listener's fd number comes back with a new socket: it must not hold a second slot */
        if (e.fds[i] == fd || e.fds[i] < 0 ||
            httpd_ws_get_fd_info(e.server, e.fds[i]) != HTTPD_WS_CLIENT_WEBSOCKET) {
            e.fds[i] = -1;
            if (slot < 0) {
                slot = i;
            }
        }
    }
    if (slot < 0) { /* full: a client that vanished without closing must not lock out the next one */
        slot = e.evict++ % CLIENTS;
        ESP_LOGW(TAG, "%d listeners already, dropping one", CLIENTS);
        httpd_sess_trigger_close(e.server, e.fds[slot]);
    }
    e.fds[slot] = fd;
    xSemaphoreGive(e.lock);
    char *buf = malloc(STATE_MAX);
    if (buf && e.state_json(buf, STATE_MAX)) {
        send_locked(fd, buf); /* the current state at once, then only changes */
    }
    free(buf);
    return ESP_OK;
}

esp_err_t events_handler(httpd_req_t *req)
{
    httpd_ws_frame_t f = {0};
    if (httpd_ws_recv_frame(req, &f, 0) != ESP_OK || f.len > FRAME_MAX) {
        return ESP_FAIL;
    }
    if (f.len == 0) {
        return ESP_OK;
    }
    char text[FRAME_MAX + 1];
    f.payload = (uint8_t *)text;
    if (httpd_ws_recv_frame(req, &f, f.len) !=
        ESP_OK) { /* also for frames we ignore: the next one starts after it */
        return ESP_FAIL;
    }
    if (f.type != HTTPD_WS_TYPE_TEXT) {
        return ESP_OK;
    }
    text[f.len] = '\0';
    command(httpd_req_to_sockfd(req), text);
    return ESP_OK;
}

void events_start(httpd_handle_t server, size_t (*state_json)(char *buf, size_t cap))
{
    e.server = server;
    e.state_json = state_json;
    e.lock = xSemaphoreCreateMutex();
    e.inputs = xQueueCreate(16, INPUT_MAX);
    e.state = calloc(1, STATE_MAX);
    for (int i = 0; i < CLIENTS; i++) {
        e.fds[i] = -1;
    }
    db_deck_listener_t l = {.input = on_input};
    db_deck_add_listener(&l);
    xTaskCreatePinnedToCore(poll_task, "events", 3072, NULL, 3, NULL, 0);
}
