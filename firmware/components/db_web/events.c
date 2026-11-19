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
#include "db_json.h"
#include "db_log.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

#define CLIENTS    8
#define STATE_MAX  STATUS_JSON_MAX /* a bigger state would stop going out */
#define POLL_MS    200
#define SCREEN_MS  50 /* how soon a changed deck picture reaches the page */
#define SCREEN_MAX 8192
#define FRAME_MAX  512
#define INPUT_MAX  224
#define LOG_CHUNK  4096 /* per listener and poll: a full log backlog goes out in about 1 s */
/* a listener hears something this often, so it can tell a dead link from a quiet deck */
#define PING_MS 5000
/* httpd_queue_work posts to a UDP socket that holds 6 messages and silently drops a 7th. So messages wait in
 * our own queue, and httpd only gets a "drain" request that owns nothing; one lost is asked again after
 * DRAIN_RETRY_MS. */
#define OUTBOX         8
#define DRAIN_RETRY_MS 1000

static const char *TAG = "events";

static struct {
    size_t (*state_json)(char *buf, size_t cap);
    size_t (*screen_json)(int deck, char *buf, size_t cap);
    SemaphoreHandle_t lock; /* the client list, and one frame on the wire at a time */
    QueueHandle_t inputs;   /* presses go out from our task: a stuck client must not stall the deck */
    QueueHandle_t events; /* numbers of new events: a stuck client must not stall the task that logged one */
    int fds[CLIENTS];
    httpd_handle_t servers[CLIENTS]; /* HTTP or HTTPS: each fd belongs to one of them */
    bool by_token[CLIENTS];          /* an integration: no simulated presses are sent to it */
    bool limited[CLIENTS];           /* a control token: may press, but not change settings or read the log */
    bool log[CLIENTS];               /* asked for the log with {"t":"log"} */
    char session[CLIENTS][65];       /* the cookie it opened with, checked again per command; empty: none */
    uint64_t log_pos[CLIENTS];
    char *state; /* the last state sent, to send only changes */
    uint32_t screen[DB_MAX_DECKS];
    char serial[DB_MAX_DECKS][32]; /* of each attached deck, "" for none; the deck's event task writes it */
    unsigned evict;
    struct outbox {
        httpd_handle_t server;
        QueueHandle_t jobs;
        uint32_t asked_ms; /* a drain is pending since then; 0: none */
    } out[2];              /* HTTP and HTTPS */
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
    if (httpd_ws_send_frame_async(server, fd, &f) != ESP_OK) { /* it reconnects and gets the whole state */
        ESP_LOGW(TAG, "send to listener %d failed, closing it", fd);
        httpd_sess_trigger_close(server, fd);
    }
}

static void send_locked(httpd_handle_t server, int fd, const char *json)
{
    xSemaphoreTake(e.lock, portMAX_DELAY);
    send_to(server, fd, json);
    xSemaphoreGive(e.lock);
}

typedef enum {
    TO_ALL,
    TO_SESSIONS, /* not to tokens: simulated presses, which an integration must not act on, and deck pictures
                  */
    TO_TOKENS,
} audience_t;

typedef struct {
    httpd_handle_t server;
    int fd; /* -1: every listener of this server */
    audience_t to;
    char json[];
} job_t;

/* In the server's own task: a socket checked here cannot close and come back as another connection before the
 * send, and a TLS session is never written while the server reads it. */
static void send_job(job_t *job)
{
    xSemaphoreTake(e.lock, portMAX_DELAY);
    for (int i = 0; i < CLIENTS; i++) {
        int fd = e.fds[i];
        if (fd < 0 || e.servers[i] != job->server || (job->fd >= 0 && fd != job->fd) ||
            (job->to == TO_SESSIONS && e.by_token[i]) || (job->to == TO_TOKENS && !e.by_token[i])) {
            continue;
        }
        if (httpd_ws_get_fd_info(job->server, fd) != HTTPD_WS_CLIENT_WEBSOCKET) {
            e.fds[i] = -1; /* gone */
            continue;
        }
        if (e.session[i][0] && !auth_session_alive(e.session[i])) {
            httpd_sess_trigger_close(job->server, fd); /* logged out, a new password or the setup over */
            e.fds[i] = -1;
            continue;
        }
        send_to(job->server, fd, job->json);
    }
    xSemaphoreGive(e.lock);
    free(job);
}

static void drain(void *arg)
{
    struct outbox *o = arg;
    __atomic_store_n(&o->asked_ms, 0, __ATOMIC_RELAXED); /* before the queue: a job added now asks again */
    job_t *job;
    while (xQueueReceive(o->jobs, (void *)&job, 0) == pdTRUE) {
        send_job(job);
    }
}

static void ask_drain(struct outbox *o)
{
    uint32_t now = (uint32_t)(esp_timer_get_time() / 1000) | 1;
    uint32_t asked = __atomic_load_n(&o->asked_ms, __ATOMIC_RELAXED);
    if ((asked && now - asked < DRAIN_RETRY_MS) ||
        !__atomic_compare_exchange_n(&o->asked_ms, &asked, now, false, __ATOMIC_RELAXED, __ATOMIC_RELAXED)) {
        return;
    }
    if (httpd_queue_work(o->server, drain, o) != ESP_OK) {
        __atomic_store_n(&o->asked_ms, 0, __ATOMIC_RELAXED);
    }
}

/* Under e.lock. */
static struct outbox *outbox(httpd_handle_t server)
{
    for (int k = 0; k < 2; k++) {
        if (!e.out[k].server) {
            e.out[k].server = server;
        }
        if (e.out[k].server == server) {
            return &e.out[k];
        }
    }
    return NULL;
}

static bool queue_job(httpd_handle_t server, int fd, const char *json, audience_t to)
{
    xSemaphoreTake(e.lock, portMAX_DELAY);
    struct outbox *o = outbox(server);
    xSemaphoreGive(e.lock);
    size_t len = strlen(json) + 1;
    job_t *job = o ? malloc(sizeof(*job) + len) : NULL;
    if (!job) {
        return false;
    }
    job->server = server;
    job->fd = fd;
    job->to = to;
    memcpy(job->json, json, len);
    bool queued = xQueueSend(o->jobs, (const void *)&job, 0) == pdTRUE;
    if (!queued) {
        free(job);
    }
    ask_drain(o);
    return queued;
}

/* false: not every server got it, so a change should be sent again at the next poll */
static bool broadcast_to(const char *json, audience_t to)
{
    if (!e.lock) {
        return true; /* before db_web_start */
    }
    httpd_handle_t servers[2] = {0};
    xSemaphoreTake(e.lock, portMAX_DELAY);
    for (int i = 0; i < CLIENTS; i++) {
        if (e.fds[i] >= 0 && servers[0] != e.servers[i]) {
            servers[servers[0] != NULL] = e.servers[i];
        }
    }
    xSemaphoreGive(e.lock);
    bool all = true;
    for (int k = 0; k < 2 && servers[k]; k++) {
        all &= queue_job(servers[k], -1, json, to);
    }
    return all;
}

static bool broadcast(const char *json)
{
    return broadcast_to(json, TO_ALL);
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
            queue_job(server, fd, json, TO_ALL);
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

/* The last inputs for GET /api/input/log: a fixed ring in PSRAM, the oldest overwritten, gone at a restart.
 */
typedef struct {
    uint32_t at_ms;
    uint8_t kind;
    uint8_t index;
    uint8_t origin; /* 0 the deck, 1 a person over the network ("real"), 2 a tool or script */
    uint8_t writer;
    int16_t value;
    uint16_t x, y;
    char deck[16]; /* its serial */
} logged_t;
static logged_t *input_log;
static uint32_t input_seq; /* inputs ever logged; entry n is at n % INPUT_LOG */

static void log_input(int deck, const db_input_t *ev)
{
    if (input_log) {
        input_log[input_seq % INPUT_LOG] = (logged_t){.at_ms = (uint32_t)(esp_timer_get_time() / 1000),
                                                      .kind = (uint8_t)ev->kind,
                                                      .index = ev->index,
                                                      .origin = db_deck_input_from_deck()   ? 0
                                                                : db_deck_input_real_for(0) ? 1
                                                                                            : 2,
                                                      .writer = (uint8_t)db_deck_writer(deck),
                                                      .value = ev->value,
                                                      .x = ev->x,
                                                      .y = ev->y};
        strlcpy(input_log[input_seq % INPUT_LOG].deck, e.serial[deck], sizeof(input_log[0].deck));
        input_seq++;
    }
}

size_t events_input_log(char *buf, size_t cap, uint32_t since, uint32_t before)
{
    static const char *const origins[] = {"deck", "web", "tool"};
    db_json_t j;
    db_json_begin(&j, buf, cap);
    db_json_arr_open(&j, NULL);
    uint32_t total = input_seq; /* entries written meanwhile show next time */
    uint32_t end = before && before - 1 < total ? before - 1 : total;
    uint32_t from = total > INPUT_LOG ? total - INPUT_LOG : 0;
    uint32_t first = since > from ? since : from;
    first = end > first && end - first > INPUT_PAGE ? end - INPUT_PAGE : first;
    for (uint32_t n = first; input_log && n < end; n++) {
        const logged_t *l = &input_log[n % INPUT_LOG];
        db_json_obj_open(&j, NULL);
        db_json_int(&j, "seq", n + 1);
        db_json_int(&j, "at_ms", l->at_ms);
        db_json_str(&j, "origin", origins[l->origin]);
        db_json_str(&j, "kind", l->kind < sizeof(kinds) / sizeof(kinds[0]) ? kinds[l->kind] : "?");
        db_json_int(&j, "index", l->index);
        db_json_int(&j, "value", l->value);
        db_json_int(&j, "x", l->x);
        db_json_int(&j, "y", l->y);
        db_json_str(&j, "writer", db_deck_source_name((db_source_t)l->writer));
        db_json_str(&j, "deck", l->deck);
        db_json_obj_close(&j);
    }
    db_json_arr_close(&j);
    return db_json_end(&j);
}

static void on_device(int deck, const db_device_info_t *info)
{
    strlcpy(e.serial[deck], info ? info->serial : "", sizeof(e.serial[deck]));
}

static void on_input(int deck, const db_input_t *ev)
{
    log_input(deck, ev);
    if (!anyone() || ev->kind >= sizeof(kinds) / sizeof(kinds[0])) {
        return;
    }
    char json[INPUT_MAX];
    snprintf(
        json, sizeof(json),
        "{\"t\":\"input\",\"kind\":\"%s\",\"index\":%u,\"value\":%d,\"x\":%u,\"y\":%u,\"x2\":%u,\"y2\":%u,"
        "\"writer\":\"%s\",\"simulated\":%s,\"deck\":\"%s\"}",
        kinds[ev->kind], ev->index, ev->value, ev->x, ev->y, ev->x2, ev->y2,
        db_deck_source_name(db_deck_writer(deck)),
        db_deck_input_real_for(0) ? "false" : "true", /* a person's */
        e.serial[deck]);
    xQueueSend(e.inputs, json, 0); /* full: the press is dropped for listeners, never for the deck */
}

static void on_event(uint32_t seq)
{
    if (e.events && anyone()) {
        xQueueSend(e.events, &seq, 0);
    }
}

static void send_event(uint32_t seq, char *buf, size_t cap)
{
    db_event_t ev;
    if (!db_log_event_get(seq, &ev)) {
        return;
    }
    db_json_t j;
    db_json_begin(&j, buf, cap);
    db_json_obj_open(&j, NULL);
    db_json_str(&j, "t", "event");
    db_log_event_json(&j, "event", &ev);
    db_json_obj_close(&j);
    if (db_json_end(&j)) {
        broadcast(buf);
    }
}

/* State and screen changes, checked a few times a second while someone listens. */
static void poll_task(void *arg)
{
    char *buf = heap_caps_malloc(STATE_MAX, MALLOC_CAP_SPIRAM);
    char *chunk = heap_caps_malloc(LOG_CHUNK + 1, MALLOC_CAP_SPIRAM);
    char *scr = heap_caps_malloc(SCREEN_MAX + 96, MALLOC_CAP_SPIRAM);
    static db_shot_t shots[DB_SCREEN_MAX];
    char input[INPUT_MAX];
    /* differences only: the tick count wraps after 49.7 days, and a plain comparison would stop the updates
     */
    TickType_t polled = xTaskGetTickCount();
    TickType_t ping = polled;
    for (;;) {
        if (xQueueReceive(e.inputs, input, pdMS_TO_TICKS(SCREEN_MS)) == pdTRUE) {
            broadcast_to(input, strstr(input, "\"simulated\":true") ? TO_SESSIONS : TO_ALL);
        }
        uint32_t seq;
        while (scr && xQueueReceive(e.events, &seq, 0) == pdTRUE) {
            send_event(seq, scr, SCREEN_MAX);
        }
        if (xTaskGetTickCount() - ping >= pdMS_TO_TICKS(PING_MS) && anyone()) {
            ping = xTaskGetTickCount();
            broadcast("{\"t\":\"ping\"}");
        }
        if (!anyone()) {
            continue;
        }
        for (int d = 0; d < DB_MAX_DECKS; d++) {
            if (!e.serial[d][0]) {
                continue;
            }
            db_source_t writer;
            size_t count = db_deck_shots(d, &writer, shots, DB_SCREEN_MAX);
            uint32_t key = db_deck_shots_key(writer, shots, count);
            if (key == e.screen[d]) {
                continue;
            }
            char json[128];
            int k = snprintf(json, sizeof(json),
                             "{\"t\":\"screen\",\"deck\":\"%s\",\"writer\":\"%s\",\"images\":%u", e.serial[d],
                             db_deck_source_name(writer), (unsigned)count);
            /* a page with a login gets the picture list along and saves a request; tokens fetch it, so the
             * deck's login code stays hidden from them */
            size_t m = 0;
            if (scr) {
                memcpy(scr, json, (size_t)k);
                /* NOLINTNEXTLINE(bugprone-not-null-terminated-result): the JSON goes on */
                memcpy(scr + k, ",\"screen\":", 10);
                m = e.screen_json(d, scr + k + 10, SCREEN_MAX);
            }
            strcpy(json + k, "}");
            if (m) {
                strcpy(scr + k + 10 + m, "}");
                bool sessions = broadcast_to(scr, TO_SESSIONS);
                if (broadcast_to(json, TO_TOKENS) && sessions) {
                    e.screen[d] = key;
                }
            } else if (broadcast(json)) {
                e.screen[d] = key;
            }
        }
        if (xTaskGetTickCount() - polled < pdMS_TO_TICKS(POLL_MS) || !buf) {
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
        if (n && strcmp(buf, e.state) != 0 && broadcast(buf)) {
            memcpy(e.state, buf, n + 1);
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
    char reply[320] = "{\"ok\":false,\"error\":\"t: set, input or log expected\"}";
    int code = 400;
    if (cJSON_IsString(t) && strcmp(t->valuestring, "log") == 0) {
        code = 403;
        snprintf(reply, sizeof(reply),
                 "{\"ok\":false,\"error\":\"needs the password, not a token\",\"code\":\"needs_password\"}");
        xSemaphoreTake(e.lock, portMAX_DELAY);
        for (int i = 0; i < CLIENTS; i++) {
            if (e.fds[i] == fd && e.servers[i] == server && !e.limited[i]) {
                e.log[i] = true; /* from the oldest line still kept: a new listener sees the boot too */
                e.log_pos[i] = 0;
                code = 200;
                snprintf(reply, sizeof(reply), "{\"ok\":true}");
            }
        }
        xSemaphoreGive(e.lock);
    } else if (cJSON_IsString(t) &&
               (strcmp(t->valuestring, "set") == 0 || strcmp(t->valuestring, "input") == 0)) {
        bool set = t->valuestring[0] == 's';
        bool token = false;
        bool any_token = false;
        char session[sizeof(e.session[0])] = "";
        xSemaphoreTake(e.lock, portMAX_DELAY);
        for (int i = 0; i < CLIENTS; i++) {
            if (e.fds[i] == fd && e.servers[i] == server) {
                token |= e.limited[i];
                any_token |= e.by_token[i];
                strcpy(session, e.session[i]);
            }
        }
        xSemaphoreGive(e.lock);
        cJSON_DeleteItemFromObjectCaseSensitive(root, "t");
        cJSON_DeleteItemFromObjectCaseSensitive(root, "id");
        char *body = cJSON_PrintUnformatted(root);
        if (session[0] && !auth_session_alive(session)) {
            snprintf(reply, sizeof(reply), "{\"ok\":false,\"error\":\"login required\"}");
            code = 401;
        } else if (set && token) {
            snprintf(
                reply, sizeof(reply),
                "{\"ok\":false,\"error\":\"needs the password, not a token\",\"code\":\"needs_password\"}");
            code = 403;
        } else if (!web_lock_for(
                       10000)) { /* as in http.c timed(): a hung handler must not hold this server */
            snprintf(reply, sizeof(reply), "{\"ok\":false,\"error\":\"busy\",\"code\":\"busy\"}");
            code = 503;
        } else {
            code = body ? (set ? (any_token ? api_config_token : api_config)(body, reply, sizeof(reply))
                               : api_input(-1, body, reply, sizeof(reply)))
                        : 500;
            web_unlock();
        }
        free(body);
    }
    char out[384];
    snprintf(out, sizeof(out), "{\"t\":\"result\",\"id\":%d,\"status\":%d,\"reply\":%s}", id, code, reply);
    cJSON_Delete(root);
    send_locked(server, fd, out);
    if (code == 401) {
        httpd_sess_trigger_close(server, fd); /* the page opens a new one with its next login */
    }
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
        slot = (int)(e.evict++ % CLIENTS);
        ESP_LOGW(TAG, "%d listeners already, dropping one", CLIENTS);
        httpd_sess_trigger_close(e.servers[slot], e.fds[slot]);
    }
    e.fds[slot] = fd;
    e.servers[slot] = req->handle;
    e.by_token[slot] = auth_by_token(req);
    e.limited[slot] = !auth_full(req);
    e.log[slot] = false;
    if (!auth_session_of(req, e.session[slot], sizeof(e.session[slot]))) {
        e.session[slot][0] = '\0';
    }
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

esp_err_t events_start(size_t (*state_json)(char *buf, size_t cap),
                       size_t (*screen_json)(int deck, char *buf, size_t cap))
{
    e.state_json = state_json;
    e.screen_json = screen_json;
    e.lock = xSemaphoreCreateMutex();
    e.inputs = xQueueCreate(16, INPUT_MAX);
    e.events = xQueueCreate(16, sizeof(uint32_t));
    input_log = heap_caps_calloc(INPUT_LOG, sizeof(logged_t), MALLOC_CAP_SPIRAM);
    e.state = heap_caps_calloc(1, STATE_MAX, MALLOC_CAP_SPIRAM);
    e.out[0].jobs = xQueueCreate(OUTBOX, sizeof(job_t *));
    e.out[1].jobs = xQueueCreate(OUTBOX, sizeof(job_t *));
    for (int i = 0; i < CLIENTS; i++) {
        e.fds[i] = -1;
    }
    if (!e.lock || !e.inputs || !e.events || !e.state || !e.out[0].jobs || !e.out[1].jobs) {
        return ESP_ERR_NO_MEM;
    }
    db_deck_listener_t l = {.input = on_input, .device_changed = on_device};
    db_deck_add_listener(&l);
    db_log_on_event(on_event);
    static db_device_info_t info; /* decks that came before the listener */
    for (int d = 0; d < DB_MAX_DECKS; d++) {
        if (db_deck_info(d, &info)) {
            on_device(d, &info);
        }
    }
    return xTaskCreatePinnedToCore(poll_task, "events", 3072, NULL, 3, NULL, 0) == pdPASS ? ESP_OK
                                                                                          : ESP_ERR_NO_MEM;
}
