#include "events.h"
#include "db_web.h"
#include "web.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "api.h"
#include "auth.h"
#include "cJSON.h"
#include "db_deck.h"
#include "db_log.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

#define CLIENTS   8
#define STATE_MAX 4096 /* as big as /api/status: a bigger state would stop going out */
#define POLL_MS   200
#define FRAME_MAX 512
#define INPUT_MAX 160
#define LOG_CHUNK 4096 /* per listener and poll: a full log backlog goes out in about 1 s */
/* a listener hears something this often, so it can tell a dead link from a quiet deck */
#define PING_MS 5000

static const char *TAG = "events";

static struct {
    size_t (*state_json)(char *buf, size_t cap);
    SemaphoreHandle_t lock; /* the client list, and one frame on the wire at a time */
    QueueHandle_t inputs;   /* presses go out from our task: a stuck client must not stall the deck */
    int fds[CLIENTS];
    httpd_handle_t servers[CLIENTS]; /* HTTP or HTTPS: each fd belongs to one of them */
    bool by_token[CLIENTS];          /* a token may press, but not change settings */
    bool log[CLIENTS];               /* asked for the log with {"t":"log"} */
    uint64_t log_pos[CLIENTS];
    char *state;                     /* the last state sent, to send only changes */
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

static void send_to(httpd_handle_t server, int fd, const char *json)
{
    if (!writable(fd)) {
        ESP_LOGW(TAG, "listener %d does not read, closing it", fd);
        httpd_sess_trigger_close(server, fd);
        return;
    }
    httpd_ws_frame_t f = {.type = HTTPD_WS_TYPE_TEXT, .payload = (uint8_t *)json, .len = strlen(json)};
    httpd_ws_send_frame_async(server, fd, &f);
}

static void send_locked(httpd_handle_t server, int fd, const char *json)
{
    xSemaphoreTake(e.lock, portMAX_DELAY);
    send_to(server, fd, json);
    xSemaphoreGive(e.lock);
}

typedef struct {
    httpd_handle_t server;
    int fd;         /* -1: every listener of this server */
    bool no_tokens; /* simulated presses: an integration must not act on them */
    char json[];
} job_t;

/* In the server's own task: a socket checked here cannot close and come back as another connection before the
 * send, and a TLS session is never written while the server reads it. */
static void send_job(void *arg)
{
    job_t *job = arg;
    xSemaphoreTake(e.lock, portMAX_DELAY);
    for (int i = 0; i < CLIENTS; i++) {
        int fd = e.fds[i];
        if (fd < 0 || e.servers[i] != job->server || (job->fd >= 0 && fd != job->fd) ||
            (job->no_tokens && e.by_token[i])) {
            continue;
        }
        if (httpd_ws_get_fd_info(job->server, fd) != HTTPD_WS_CLIENT_WEBSOCKET) {
            e.fds[i] = -1; /* gone */
            continue;
        }
        send_to(job->server, fd, job->json);
    }
    xSemaphoreGive(e.lock);
    free(job);
}

static void queue_job(httpd_handle_t server, int fd, const char *json, bool no_tokens)
{
    size_t len = strlen(json) + 1;
    job_t *job = malloc(sizeof(*job) + len);
    if (!job) {
        return;
    }
    job->server = server;
    job->fd = fd;
    job->no_tokens = no_tokens;
    memcpy(job->json, json, len);
    if (httpd_queue_work(server, send_job, job) != ESP_OK) {
        free(job);
    }
}

static void broadcast_to(const char *json, bool no_tokens)
{
    if (!e.lock) {
        return; /* before db_web_start */
    }
    httpd_handle_t servers[2] = {0};
    xSemaphoreTake(e.lock, portMAX_DELAY);
    for (int i = 0; i < CLIENTS; i++) {
        if (e.fds[i] >= 0 && servers[0] != e.servers[i]) {
            servers[servers[0] != NULL] = e.servers[i];
        }
    }
    xSemaphoreGive(e.lock);
    for (int k = 0; k < 2 && servers[k]; k++) {
        queue_job(servers[k], -1, json, no_tokens);
    }
}

static void broadcast(const char *json)
{
    broadcast_to(json, false);
}

/* New log text to each listener that asked for it, from where it left off. */
static void send_log(char *chunk)
{
    for (int i = 0; i < CLIENTS; i++) {
        xSemaphoreTake(e.lock, portMAX_DELAY);
        bool on = e.log[i] && e.fds[i] >= 0;
        uint64_t pos = e.log_pos[i];
        int fd = e.fds[i];
        httpd_handle_t server = e.servers[i];
        xSemaphoreGive(e.lock);
        size_t n = on ? db_log_since(&pos, chunk, LOG_CHUNK) : 0;
        if (!n) {
            continue;
        }
        chunk[n] = '\0';
        cJSON *msg = cJSON_CreateObject();
        cJSON_AddStringToObject(msg, "t", "log");
        cJSON_AddStringToObject(msg, "text", chunk);
        char *json = cJSON_PrintUnformatted(msg);
        cJSON_Delete(msg);
        if (json) {
            queue_job(server, fd, json, false);
            free(json);
        }
        xSemaphoreTake(e.lock, portMAX_DELAY);
        if (e.fds[i] == fd) {
            e.log_pos[i] = pos;
        }
        xSemaphoreGive(e.lock);
    }
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
        "\"writer\":\"%s\",\"simulated\":%s}",
        kinds[ev->kind], ev->index, ev->value, ev->x, ev->y, ev->x2, ev->y2,
        db_deck_source_name(db_deck_writer()), db_deck_input_real_for(0) ? "false" : "true"); /* a person's, not a tool's */
    xQueueSend(e.inputs, json, 0); /* full: the press is dropped for listeners, never for the deck */
}

/* State and screen changes, checked a few times a second while someone listens. */
static void poll_task(void *arg)
{
    char *buf = heap_caps_malloc(STATE_MAX, MALLOC_CAP_SPIRAM);
    char *chunk = heap_caps_malloc(LOG_CHUNK + 1, MALLOC_CAP_SPIRAM);
    static db_shot_t shots[DB_SCREEN_MAX];
    char input[INPUT_MAX];
    /* differences only: the tick count wraps after 49.7 days, and a plain comparison would stop the updates
     */
    TickType_t polled = xTaskGetTickCount();
    TickType_t ping = polled;
    for (;;) {
        if (xQueueReceive(e.inputs, input, pdMS_TO_TICKS(POLL_MS)) == pdTRUE) {
            broadcast_to(input, strstr(input, "\"simulated\":true") != NULL);
        }
        if (xTaskGetTickCount() - ping >= pdMS_TO_TICKS(PING_MS) && anyone()) {
            ping = xTaskGetTickCount();
            broadcast("{\"t\":\"ping\"}");
        }
        if (xTaskGetTickCount() - polled < pdMS_TO_TICKS(POLL_MS) || !anyone() || !buf) {
            continue;
        }
        polled = xTaskGetTickCount();
        if (chunk) {
            send_log(chunk);
        }
        size_t n = e.state_json(buf, STATE_MAX);
        static bool warned;
        if (!n && !warned) {
            warned = true;
            ESP_LOGW(TAG, "state does not fit %d bytes, listeners stop getting it", STATE_MAX);
        }
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
    char origin[128] = {0};
    char host[128] = {0};
    esp_err_t got = httpd_req_get_hdr_value_str(req, "Origin", origin, sizeof(origin));
    if (got == ESP_ERR_NOT_FOUND) {
        return true; /* not a browser: scripts and Home Assistant */
    }
    if (got != ESP_OK) {
        return false; /* too long to compare: not let through as if it were missing */
    }
    httpd_req_get_hdr_value_str(req, "Host", host, sizeof(host));
    const char *o = strstr(origin, "://");
    return o && strcmp(o + 3, host) == 0;
}

static void command(httpd_handle_t server, int fd, const char *text)
{
    cJSON *root = cJSON_Parse(text);
    const cJSON *t = cJSON_GetObjectItemCaseSensitive(root, "t");
    const cJSON *id_item = cJSON_GetObjectItemCaseSensitive(root, "id");
    int id = cJSON_IsNumber(id_item) ? id_item->valueint : 0; /* read before the item is deleted below */
    char reply[160] = "{\"ok\":false,\"error\":\"t: set, input or log expected\"}";
    int code = 400;
    if (cJSON_IsString(t) && strcmp(t->valuestring, "log") == 0) {
        code = 403;
        snprintf(reply, sizeof(reply), "{\"ok\":false,\"error\":\"needs the password, not a token\"}");
        xSemaphoreTake(e.lock, portMAX_DELAY);
        for (int i = 0; i < CLIENTS; i++) {
            if (e.fds[i] == fd && e.servers[i] == server && !e.by_token[i]) {
                e.log[i] = true; /* from the oldest line still kept: a new listener sees the boot too */
                e.log_pos[i] = 0;
                code = 200;
                snprintf(reply, sizeof(reply), "{\"ok\":true}");
            }
        }
        xSemaphoreGive(e.lock);
    } else if (cJSON_IsString(t) && (strcmp(t->valuestring, "set") == 0 || strcmp(t->valuestring, "input") == 0)) {
        bool set = t->valuestring[0] == 's';
        bool token = false;
        xSemaphoreTake(e.lock, portMAX_DELAY);
        for (int i = 0; i < CLIENTS; i++) {
            token |= e.fds[i] == fd && e.servers[i] == server && e.by_token[i];
        }
        xSemaphoreGive(e.lock);
        cJSON_DeleteItemFromObjectCaseSensitive(root, "t");
        cJSON_DeleteItemFromObjectCaseSensitive(root, "id");
        char *body = cJSON_PrintUnformatted(root);
        if (set && token) {
            snprintf(reply, sizeof(reply), "{\"ok\":false,\"error\":\"needs the password, not a token\"}");
            code = 403;
        } else {
            web_lock();
            code =
                body ? (set ? api_config(body, reply, sizeof(reply)) : api_input(body, reply, sizeof(reply)))
                     : 500;
            web_unlock();
        }
        free(body);
    }
    char out[224];
    snprintf(out, sizeof(out), "{\"t\":\"result\",\"id\":%d,\"status\":%d,\"reply\":%s}", id, code, reply);
    cJSON_Delete(root);
    send_locked(server, fd, out);
}

/* Before the upgrade, so a refused socket gets a plain 401 or 403 and never opens. */
esp_err_t events_check(httpd_req_t *req)
{
    if (web_plain_refused(req)) {
        httpd_resp_send_err(req, HTTPD_403_FORBIDDEN, "HTTPS only");
        return ESP_FAIL;
    }
    if (!same_origin(req)) {
        httpd_resp_send_err(req, HTTPD_403_FORBIDDEN, "other origin");
        return ESP_FAIL;
    }
    return auth_check(req, WEB_CONTROL) ? ESP_OK : ESP_FAIL;
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
            httpd_ws_get_fd_info(e.servers[i], e.fds[i]) != HTTPD_WS_CLIENT_WEBSOCKET) {
            e.fds[i] = -1;
            if (slot < 0) {
                slot = i;
            }
        }
    }
    if (slot < 0) { /* full: a client that vanished without closing must not lock out the next one */
        slot = e.evict++ % CLIENTS;
        ESP_LOGW(TAG, "%d listeners already, dropping one", CLIENTS);
        httpd_sess_trigger_close(e.servers[slot], e.fds[slot]);
    }
    e.fds[slot] = fd;
    e.servers[slot] = req->handle;
    e.by_token[slot] = auth_by_token(req);
    e.log[slot] = false;
    char *buf = heap_caps_malloc(STATE_MAX, MALLOC_CAP_SPIRAM);
    if (buf && e.state_json(buf, STATE_MAX)) {
        send_to(req->handle, fd, buf); /* under the lock: the current state comes before any change */
    }
    xSemaphoreGive(e.lock);
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
    command(req->handle, httpd_req_to_sockfd(req), text);
    return ESP_OK;
}

void db_web_broadcast(const char *json)
{
    broadcast(json);
}

void events_start(size_t (*state_json)(char *buf, size_t cap))
{
    e.state_json = state_json;
    e.lock = xSemaphoreCreateMutex();
    e.inputs = xQueueCreate(16, INPUT_MAX);
    e.state = heap_caps_calloc(1, STATE_MAX, MALLOC_CAP_SPIRAM);
    for (int i = 0; i < CLIENTS; i++) {
        e.fds[i] = -1;
    }
    db_deck_listener_t l = {.input = on_input};
    db_deck_add_listener(&l);
    xTaskCreatePinnedToCore(poll_task, "events", 3072, NULL, 3, NULL, 0);
}
