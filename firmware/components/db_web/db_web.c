#include "db_web.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "api.h"
#include "auth.h"
#include "cJSON.h"
#include "db_config.h"
#include "db_deck.h"
#include "db_json.h"
#include "db_log.h"
#include "db_net.h"
#include "db_ota.h"
#include "db_screen.h"
#include "db_ui.h"
#include "esp_app_desc.h"
#include "esp_heap_caps.h"
#include "esp_http_server.h"
#include "esp_image_format.h"
#include "esp_log.h"
#include "esp_ota_ops.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "events.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "lwip/sockets.h"
#include "mdns.h"
#include "nvs.h"
#include "web.h"

#define BODY_MAX        2048
#define CHUNK           4096
#define JSON_MAX        2048
#define SCREEN_JSON_MAX 8192
#define STATUS_JSON_MAX 4096

static const char *TAG = "web";

extern const char index_html_start[] asm("_binary_index_html_start");
extern const char index_html_end[] asm("_binary_index_html_end");
extern const char index_gz_start[] asm("_binary_index_html_gz_start");
extern const char index_gz_end[] asm("_binary_index_html_gz_end");

static size_t (*status_json)(char *buf, size_t cap);

/* Every route but the web socket runs through timed(): the login check, and the time for /api/profile. The
 * server runs one handler at a time, so no lock. */
typedef struct {
    esp_err_t (*handler)(httpd_req_t *req);
    bool open; /* no login needed */
    uint32_t count;
    uint32_t max_us;
    uint64_t total_us;
} route_t;

static httpd_uri_t uris[20];
static route_t routes[20];
static size_t route_count;

esp_err_t send_json(httpd_req_t *req, const char *json)
{
    httpd_resp_set_type(req, "application/json");
    httpd_resp_set_hdr(req, "Cache-Control", "no-store");
    return httpd_resp_sendstr(req, json);
}

esp_err_t send_error(httpd_req_t *req, const char *status, const char *msg)
{
    char buf[200];
    snprintf(buf, sizeof(buf), "{\"ok\":false,\"error\":\"%s\"}", msg);
    httpd_resp_set_status(req, status);
    return send_json(req, buf);
}

int read_body(httpd_req_t *req, char *buf, size_t cap)
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
    vTaskDelay(pdMS_TO_TICKS(1000)); /* the reply goes out first */
    esp_restart();
}

void restart_soon(void)
{
    xTaskCreate(restart_task, "web_restart", 2048, NULL, 5, NULL);
}

/* The page itself is public; it shows the login form until /api/status answers. It only changes with the
 * firmware, so its ETag is the firmware's hash and a browser that has it gets 304. */
static esp_err_t page(httpd_req_t *req)
{
    char accept[64] = {0};
    httpd_req_get_hdr_value_str(req, "Accept-Encoding", accept, sizeof(accept));
    bool gz = strstr(accept, "gzip") != NULL;
    char etag[24];
    const uint8_t *sha = esp_app_get_description()->app_elf_sha256;
    snprintf(etag, sizeof(etag), "\"%02x%02x%02x%02x%02x%02x%02x%02x%s\"", sha[0], sha[1], sha[2], sha[3],
             sha[4], sha[5], sha[6], sha[7], gz ? "-gz" : ""); /* each encoding is its own body */
    char given[sizeof(etag)] = {0};
    httpd_req_get_hdr_value_str(req, "If-None-Match", given, sizeof(given));
    httpd_resp_set_hdr(req, "ETag", etag);
    httpd_resp_set_hdr(req, "Cache-Control", "no-cache");
    httpd_resp_set_hdr(req, "Vary", "Accept-Encoding");
    if (strcmp(given, etag) == 0) {
        httpd_resp_set_status(req, "304 Not Modified");
        return httpd_resp_send(req, NULL, 0);
    }
    httpd_resp_set_type(req, "text/html; charset=utf-8");
    if (gz) {
        httpd_resp_set_hdr(req, "Content-Encoding", "gzip");
        return httpd_resp_send(req, index_gz_start, index_gz_end - index_gz_start);
    }
    return httpd_resp_send(req, index_html_start,
                           index_html_end - index_html_start - 1); /* EMBED_TXTFILES adds a NUL */
}

/* A reply built into a heap buffer; build returns 0 when it does not fit. */
static esp_err_t send_built(httpd_req_t *req, size_t cap, size_t (*build)(char *buf, size_t cap))
{
    char *buf = malloc(cap);
    size_t len = buf ? build(buf, cap) : 0;
    esp_err_t err = len ? send_json(req, buf)
                        : send_error(req, "500 Internal Server Error", buf ? "too large" : "memory");
    free(buf);
    return err;
}

static esp_err_t get_status(httpd_req_t *req)
{
    return send_built(req, STATUS_JSON_MAX, status_json);
}

static esp_err_t get_config(httpd_req_t *req)
{
    char buf[JSON_MAX];
    db_config_to_json(db_config(), false, buf, sizeof(buf));
    return send_json(req, buf);
}

static const char *status_line(int code)
{
    switch (code) {
    case 200:
        return "200 OK";
    case 400:
        return "400 Bad Request";
    case 409:
        return "409 Conflict";
    case 503:
        return "503 Service Unavailable";
    default:
        return "500 Internal Server Error";
    }
}

static esp_err_t reply_with(httpd_req_t *req, int code, const char *reply)
{
    httpd_resp_set_status(req, status_line(code));
    return send_json(req, reply);
}

/* A JSON body handed to one of the api_* functions shared with the web socket. */
static esp_err_t body_api(httpd_req_t *req, int (*api)(const char *body, char *reply, size_t cap))
{
    char body[BODY_MAX];
    if (read_body(req, body, sizeof(body)) < 0) {
        return send_error(req, "413 Payload Too Large", "body too large");
    }
    char reply[160];
    return reply_with(req, api(body, reply, sizeof(reply)), reply);
}

static esp_err_t put_config(httpd_req_t *req)
{
    return body_api(req, api_config);
}

static esp_err_t post_input(httpd_req_t *req)
{
    return body_api(req, api_input);
}

static esp_err_t post_menu(httpd_req_t *req)
{
    if (db_ui_open_menu() != ESP_OK) {
        return send_error(req, "409 Conflict", "no deck connected");
    }
    return send_json(req, "{\"ok\":true}");
}

static esp_err_t post_ota(httpd_req_t *req)
{
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
    return send_json(req, "{\"ok\":true,\"restart\":true}");
}

static esp_err_t post_reboot(httpd_req_t *req)
{
    send_json(req, "{\"ok\":true}");
    restart_soon();
    return ESP_OK;
}

static esp_err_t get_log(httpd_req_t *req)
{
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

static void write_fields(db_json_t *j, db_rect_t r)
{
    db_json_int(j, "x", r.x);
    db_json_int(j, "y", r.y);
    db_json_int(j, "w", r.w);
    db_json_int(j, "h", r.h);
}

static void write_rect(db_json_t *j, db_rect_t r)
{
    db_json_obj_open(j, NULL);
    write_fields(j, r);
    db_json_obj_close(j);
}

static void write_geometry(db_json_t *j, const db_device_info_t *dev)
{
    const db_display_t *d = &dev->displays[0];
    db_json_int(j, "width", d->width);
    db_json_int(j, "height", d->height);
    db_json_arr_open(j, "keys");
    for (uint8_t k = 0; k < dev->key_count; k++) {
        for (uint8_t t = 0; t < d->target_count; t++) {
            if (d->targets[t].kind == DB_TARGET_KEY && d->targets[t].index == k) {
                write_rect(j, d->targets[t].rect);
            }
        }
    }
    db_json_arr_close(j);
    db_json_arr_open(j, "touch");
    for (uint8_t t = 0; t < dev->touch_count; t++) {
        write_rect(j, dev->touch[t].rect);
    }
    db_json_arr_close(j);
    db_json_int(j, "encoders", dev->encoder_count);
    int last;
    bool on_encoders = db_ui_menu_chord(dev->encoder_count, dev->key_count, &last);
    db_json_obj_open(j, "menu_chord");
    db_json_str(j, "on", on_encoders ? "encoders" : "keys");
    db_json_int(j, "first", 0);
    db_json_int(j, "last", last);
    db_json_obj_close(j);
}

static size_t screen_json(char *buf, size_t cap)
{
    static db_device_info_t dev; /* too big for the stack; one handler at a time */
    static db_shot_t shots[DB_SCREEN_MAX];
    db_source_t writer;
    size_t n = db_deck_shots(&writer, shots, DB_SCREEN_MAX);
    bool present = db_deck_info(&dev);
    db_json_t j;
    db_json_begin(&j, buf, cap);
    db_json_obj_open(&j, NULL);
    db_json_str(&j, "writer", db_deck_source_name(writer));
    if (present) {
        write_geometry(&j, &dev);
    }
    db_json_arr_open(&j, "images");
    for (size_t i = 0; present && i < n; i++) {
        char url[32];
        snprintf(url, sizeof(url), "/api/screen/%lu.jpg", (unsigned long)shots[i].id);
        db_json_obj_open(&j, NULL);
        write_fields(&j, shots[i].rect);
        db_json_str(&j, "url", url);
        db_json_obj_close(&j);
    }
    db_json_arr_close(&j);
    db_json_obj_close(&j);
    return db_json_end(&j);
}

static esp_err_t get_screen(httpd_req_t *req)
{
    return send_built(req, SCREEN_JSON_MAX, screen_json);
}

/* An image never changes under its id, so browsers may keep it. */
static esp_err_t get_screen_image(httpd_req_t *req)
{
    unsigned long id = strtoul(req->uri + strlen("/api/screen/"), NULL, 10);
    uint8_t *data = NULL;
    size_t len = 0;
    esp_err_t found = db_deck_shot_copy((uint32_t)id, &data, &len);
    if (found == ESP_ERR_NO_MEM) {
        return send_error(req, "500 Internal Server Error", "memory");
    }
    if (found != ESP_OK) {
        return send_error(req, "404 Not Found", "image no longer shown");
    }
    httpd_resp_set_type(req, "image/jpeg");
    httpd_resp_set_hdr(req, "Cache-Control", "private, max-age=3600, immutable");
    esp_err_t err = httpd_resp_send(req, (const char *)data, (ssize_t)len);
    heap_caps_free(data);
    return err;
}

/* The picture on the deck as one JPEG, for a camera in Home Assistant. */
static esp_err_t get_screen_jpg(httpd_req_t *req)
{
    uint8_t *jpeg = NULL;
    size_t len = 0;
    esp_err_t err = db_ui_snapshot(&jpeg, &len);
    if (err == ESP_ERR_INVALID_STATE) {
        return send_error(req, "409 Conflict", "no Stream Deck attached");
    }
    if (err != ESP_OK) {
        return send_error(req, "500 Internal Server Error", esp_err_to_name(err));
    }
    httpd_resp_set_type(req, "image/jpeg");
    httpd_resp_set_hdr(req, "Cache-Control", "no-store");
    err = httpd_resp_send(req, (const char *)jpeg, (ssize_t)len);
    heap_caps_free(jpeg);
    return err;
}

static esp_err_t post_ota_url(httpd_req_t *req)
{
    char body[256];
    if (read_body(req, body, sizeof(body)) < 0) {
        return send_error(req, "413 Payload Too Large", "body too large");
    }
    cJSON *root = cJSON_Parse(body);
    const cJSON *url = cJSON_GetObjectItemCaseSensitive(root, "url");
    bool ok = cJSON_IsString(url) && strncmp(url->valuestring, "http://", 7) == 0;
    esp_err_t err = ok ? db_ota_start_url(url->valuestring) : ESP_ERR_INVALID_ARG;
    cJSON_Delete(root);
    if (!ok) {
        return send_error(req, "400 Bad Request", "url: http://… to a signed .dbfw file expected");
    }
    if (err != ESP_OK) {
        return send_error(req, "409 Conflict", "an update is already running");
    }
    /* progress shows on the deck and in the state of /api/ws; the bridge restarts when done */
    return send_json(req, "{\"ok\":true}");
}

static esp_err_t timed(httpd_req_t *req)
{
    route_t *r = req->user_ctx;
    int64_t start = esp_timer_get_time();
    esp_err_t err = r->open || auth_check(req) ? r->handler(req) : ESP_OK;
    uint32_t us = (uint32_t)(esp_timer_get_time() - start);
    r->count++;
    r->total_us += us;
    r->max_us = us > r->max_us ? us : r->max_us;
    return err;
}

/* Per mille of all CPU time (both cores, idle included) since the previous call; since boot on the first call
 * and for a new task. */
static void write_cpu(db_json_t *j)
{
    static TaskStatus_t *last;
    static UBaseType_t last_n;
    UBaseType_t cap =
        uxTaskGetNumberOfTasks() + 4; /* a task created meanwhile would make the call return 0 */
    TaskStatus_t *now = malloc(cap * sizeof(*now));
    uint64_t *ran = malloc(cap * sizeof(*ran));
    UBaseType_t n = now && ran ? uxTaskGetSystemState(now, cap, NULL) : 0;
    if (!n) {
        free(now);
        free(ran);
        db_json_null(j, "cpu_permille");
        return;
    }
    uint64_t total = 0;
    for (UBaseType_t i = 0; i < n; i++) {
        ran[i] = now[i].ulRunTimeCounter;
        for (UBaseType_t k = 0; k < last_n; k++) {
            /* the number, not the handle: a new task can get a deleted one's memory */
            if (last[k].xTaskNumber == now[i].xTaskNumber) {
                ran[i] -= last[k].ulRunTimeCounter;
            }
        }
        total += ran[i];
    }
    db_json_obj_open(j, "cpu_permille");
    for (UBaseType_t i = 0; i < n && total; i++) {
        db_json_int(j, now[i].pcTaskName, (int64_t)(ran[i] * 1000 / total));
    }
    db_json_obj_close(j);
    free(ran);
    free(last);
    last = now;
    last_n = n;
}

static size_t profile_json(char *buf, size_t cap)
{
    db_json_t j;
    db_json_begin(&j, buf, cap);
    db_json_obj_open(&j, NULL);
    db_json_arr_open(&j, "routes");
    for (size_t i = 0; i < route_count; i++) {
        const route_t *r = &routes[i];
        if (!r->count) {
            continue;
        }
        db_json_obj_open(&j, NULL);
        db_json_str(&j, "method", http_method_str(uris[i].method));
        db_json_str(&j, "uri", uris[i].uri);
        db_json_int(&j, "count", r->count);
        db_json_int(&j, "avg_us", (int64_t)(r->total_us / r->count));
        db_json_int(&j, "max_us", r->max_us);
        db_json_obj_close(&j);
    }
    db_json_arr_close(&j);
    write_cpu(&j);
    db_json_obj_close(&j);
    return db_json_end(&j);
}

/* Load over the last minutes, sampled into RAM only: nothing of it is ever written to flash. */
#define METRIC_EVERY_US (5 * 1000000LL)
#define METRIC_SAMPLES  120 /* 10 minutes */

static struct {
    portMUX_TYPE mux;
    uint8_t cpu[METRIC_SAMPLES], heap[METRIC_SAMPLES], psram[METRIC_SAMPLES]; /* percent used */
    uint16_t next, count;
    uint64_t idle, at;
} metrics = {.mux = portMUX_INITIALIZER_UNLOCKED};

static uint64_t idle_time(void)
{
    uint64_t sum = 0;
    for (int core = 0; core < portNUM_PROCESSORS; core++) {
        TaskStatus_t st;
        vTaskGetInfo(xTaskGetIdleTaskHandleForCore(core), &st, pdFALSE, eRunning);
        sum += st.ulRunTimeCounter;
    }
    return sum;
}

static uint8_t used_pct(uint32_t caps)
{
    size_t total = heap_caps_get_total_size(caps);
    return total ? (uint8_t)(100 - heap_caps_get_free_size(caps) * 100 / total) : 0;
}

static void sample_metrics(void *arg)
{
    uint64_t now = esp_timer_get_time();
    uint64_t idle = idle_time();
    uint64_t span = (now - metrics.at) * portNUM_PROCESSORS; /* the run time counters count microseconds */
    uint8_t cpu = metrics.at && span ? (uint8_t)(100 - (idle - metrics.idle) * 100 / span) : 0;
    uint8_t heap =
        used_pct(MALLOC_CAP_INTERNAL); /* outside the critical section: the heap takes its own lock */
    uint8_t psram = used_pct(MALLOC_CAP_SPIRAM);
    taskENTER_CRITICAL(&metrics.mux);
    if (metrics.at) {
        metrics.cpu[metrics.next] = cpu;
        metrics.heap[metrics.next] = heap;
        metrics.psram[metrics.next] = psram;
        metrics.next = (metrics.next + 1) % METRIC_SAMPLES;
        metrics.count += metrics.count < METRIC_SAMPLES;
    }
    metrics.idle = idle;
    metrics.at = now;
    taskEXIT_CRITICAL(&metrics.mux);
}

static void write_series(db_json_t *j, const char *key, const uint8_t *ring)
{
    db_json_arr_open(j, key);
    for (int i = 0; i < metrics.count; i++) { /* oldest first */
        db_json_int(j, NULL, ring[(metrics.next + METRIC_SAMPLES - metrics.count + i) % METRIC_SAMPLES]);
    }
    db_json_arr_close(j);
}

/* Size of the running firmware in its slot, read once: it walks the whole image. */
static void write_flash(db_json_t *j)
{
    static uint32_t app_len;
    const esp_partition_t *part = esp_ota_get_running_partition();
    if (!app_len) {
        esp_partition_pos_t pos = {.offset = part->address, .size = part->size};
        esp_image_metadata_t meta;
        app_len = esp_image_verify(ESP_IMAGE_VERIFY_SILENT, &pos, &meta) == ESP_OK ? meta.image_len : 0;
    }
    db_json_obj_open(j, "firmware");
    db_json_int(j, "used", app_len);
    db_json_int(j, "size", part->size);
    db_json_obj_close(j);
    nvs_stats_t nvs;
    if (nvs_get_stats(NULL, &nvs) == ESP_OK) {
        db_json_obj_open(j, "settings");
        db_json_int(j, "used", nvs.used_entries);
        db_json_int(j, "size", nvs.total_entries);
        db_json_obj_close(j);
    }
}

static size_t metrics_json(char *buf, size_t cap)
{
    static uint8_t cpu[METRIC_SAMPLES], heap[METRIC_SAMPLES], psram[METRIC_SAMPLES];
    taskENTER_CRITICAL(&metrics.mux); /* a copy, so the JSON is built outside the critical section */
    memcpy(cpu, metrics.cpu, sizeof(cpu));
    memcpy(heap, metrics.heap, sizeof(heap));
    memcpy(psram, metrics.psram, sizeof(psram));
    taskEXIT_CRITICAL(&metrics.mux);
    db_json_t j;
    db_json_begin(&j, buf, cap);
    db_json_obj_open(&j, NULL);
    db_json_int(&j, "every_s", METRIC_EVERY_US / 1000000);
    write_series(&j, "cpu", cpu);
    write_series(&j, "ram", heap);
    write_series(&j, "psram", psram);
    write_flash(&j);
    db_json_obj_close(&j);
    return db_json_end(&j);
}

static esp_err_t get_metrics(httpd_req_t *req)
{
    return send_built(req, STATUS_JSON_MAX, metrics_json);
}

static esp_err_t get_profile(httpd_req_t *req)
{
    return send_built(req, STATUS_JSON_MAX, profile_json);
}

/* Without it a small reply waits for the client's delayed ACK of the headers: 40 ms instead of 1. */
static esp_err_t no_delay(httpd_handle_t hd, int fd)
{
    int one = 1;
    setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &one, sizeof(one));
    return ESP_OK;
}

/* Home Assistant and scripts find the bridge's API by this. */
static void announce_api(void)
{
    const char *fw = esp_app_get_description()->version;
    mdns_txt_item_t txt[] = {{"path", "/api"}, {"fw", fw}};
    esp_err_t err = mdns_service_add(NULL, "_deckbridge-api", "_tcp", 80, txt, sizeof(txt) / sizeof(txt[0]));
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "mDNS API announcement failed: %s", esp_err_to_name(err));
    }
}

/* Phones probe some URL when they join a WLAN; sending them to the page opens it by itself. */
static esp_err_t not_found(httpd_req_t *req, httpd_err_code_t code)
{
    db_net_status_t n;
    db_net_status(&n);
    if (strncmp(req->uri, "/api/", 5) == 0) {
        return send_error(req, "404 Not Found", "no such endpoint");
    }
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

esp_err_t db_web_start(size_t (*status)(char *buf, size_t cap), size_t (*state)(char *buf, size_t cap))
{
    status_json = status;
    httpd_config_t cfg = HTTPD_DEFAULT_CONFIG();
    cfg.stack_size = 8192;
    cfg.max_uri_handlers = sizeof(uris) / sizeof(uris[0]);
    /* several Home Assistants hold a socket and a web socket each; with the dock and the links this stays
     * under CONFIG_LWIP_MAX_SOCKETS */
    cfg.max_open_sockets = 12;
    cfg.backlog_conn = cfg.max_open_sockets; /* a refused SYN costs the client a 1 s retry */
    cfg.open_fn = no_delay;
    cfg.uri_match_fn = httpd_uri_match_wildcard;
    cfg.lru_purge_enable = true;
    cfg.recv_wait_timeout =
        5; /* a client that stops halfway through its headers blocks the server this long */
    httpd_handle_t server;
    esp_err_t err = httpd_start(&server, &cfg);
    if (err != ESP_OK) {
        return err;
    }
    const httpd_uri_t list[] = {
        {.uri = "/", .method = HTTP_GET, .handler = page},
        {.uri = "/api/status", .method = HTTP_GET, .handler = get_status},
        {.uri = "/api/config", .method = HTTP_GET, .handler = get_config},
        {.uri = "/api/config", .method = HTTP_PUT, .handler = put_config},
        {.uri = "/api/ota", .method = HTTP_POST, .handler = post_ota},
        {.uri = "/api/reboot", .method = HTTP_POST, .handler = post_reboot},
        {.uri = "/api/log", .method = HTTP_GET, .handler = get_log},
        {.uri = "/api/screen", .method = HTTP_GET, .handler = get_screen},
        {.uri = "/api/screen/*", .method = HTTP_GET, .handler = get_screen_image},
        {.uri = "/api/input", .method = HTTP_POST, .handler = post_input},
        {.uri = "/api/menu", .method = HTTP_POST, .handler = post_menu},
        {.uri = "/api/screen.jpg", .method = HTTP_GET, .handler = get_screen_jpg},
        {.uri = "/api/ota/url", .method = HTTP_POST, .handler = post_ota_url},
        {.uri = "/api/ws",
         .method = HTTP_GET,
         .handler = events_handler,
         .is_websocket = true,
         .ws_pre_handshake_cb = events_check,
         .ws_post_handshake_cb = events_open},
        {.uri = "/api/login", .method = HTTP_POST, .handler = auth_login},
        {.uri = "/api/logout", .method = HTTP_POST, .handler = auth_logout},
        {.uri = "/api/profile", .method = HTTP_GET, .handler = get_profile},
        {.uri = "/api/metrics", .method = HTTP_GET, .handler = get_metrics},
    };
    _Static_assert(sizeof(list) <= sizeof(uris), "more routes than slots");
    route_count = sizeof(list) / sizeof(list[0]);
    for (size_t i = 0; i < route_count; i++) {
        uris[i] = list[i];
        if (!list[i]
                 .is_websocket) { /* its handler runs once per frame, and checks the login at the handshake */
            routes[i].open =
                list[i].handler == page || list[i].handler == auth_login || list[i].handler == auth_logout;
            routes[i].handler = list[i].handler;
            uris[i].handler = timed;
            uris[i].user_ctx = &routes[i];
        }
        httpd_register_uri_handler(server, &uris[i]);
    }
    httpd_register_err_handler(server, HTTPD_404_NOT_FOUND, not_found);
    events_start(server, state);
    esp_timer_handle_t timer;
    esp_timer_create_args_t every = {.callback = sample_metrics, .name = "metrics"};
    if (esp_timer_create(&every, &timer) == ESP_OK) {
        esp_timer_start_periodic(timer, METRIC_EVERY_US);
    }
    announce_api();
    xTaskCreate(dns_task, "dns", 3072, NULL, 3, NULL);
    ESP_LOGI(TAG, "web UI on port 80");
    return ESP_OK;
}
