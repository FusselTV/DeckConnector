#include <math.h>
#include <stdlib.h>

#include "cJSON.h"
#include "db_json.h"
#include "db_link.h"
#include "db_net.h"
#include "db_trace.h"
#include "db_web.h"
#include "driver/temperature_sensor.h"
#include "esp_heap_caps.h"
#include "esp_image_format.h"
#include "esp_ota_ops.h"
#include "esp_partition.h"
#include "esp_timer.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "jpeg_port.h"
#include "miniz.h"
#include "nvs.h"
#include "web.h"

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
    /* smallest free stack each task ever had, to size the stacks: a stack scan, too slow for the status */
    db_json_obj_open(j, "stack_free");
    for (UBaseType_t i = 0; i < n; i++) {
        db_json_int(j, now[i].pcTaskName, now[i].usStackHighWaterMark);
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
    for (size_t i = 0; i < web_route_count; i++) {
        const route_t *r = &web_routes[i];
        if (!r->count) {
            continue;
        }
        db_json_obj_open(&j, NULL);
        db_json_str(&j, "method", http_method_str(web_uris[i].method));
        db_json_str(&j, "uri", web_uris[i].uri);
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
    uint8_t temp[METRIC_SAMPLES];                                             /* the chip's, in °C */
    uint16_t next, count;
    uint64_t idle, at;
} metrics = {.mux = portMUX_INITIALIZER_UNLOCKED};

static temperature_sensor_handle_t tsens;
static uint8_t temp_now; /* 0 until the first sample */

int db_web_temp_c(void)
{
    return temp_now;
}

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
    float c = 0;
    /* a failed read keeps the last value: a 0 would draw a drop to 0 °C */
    uint8_t temp = tsens && temperature_sensor_get_celsius(tsens, &c) == ESP_OK && c > 0 ? (uint8_t)lroundf(c)
                                                                                         : temp_now;
    taskENTER_CRITICAL(&metrics.mux);
    temp_now = temp;
    if (metrics.at) {
        metrics.temp[metrics.next] = temp;
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
    db_json_obj_open(j, "flash");
    db_json_obj_open(j, "firmware");
    db_json_int(j, "used", app_len);
    db_json_int(j, "size", part->size);
    db_json_obj_close(j);
    nvs_stats_t nvs;
    if (nvs_get_stats(NULL, &nvs) == ESP_OK) { /* an NVS entry is 32 bytes */
        db_json_obj_open(j, "settings");
        db_json_int(j, "used", nvs.used_entries * 32);
        db_json_int(j, "size", nvs.total_entries * 32);
        db_json_obj_close(j);
    }
    db_json_obj_close(j);
}

static size_t metrics_json(char *buf, size_t cap)
{
    static uint8_t cpu[METRIC_SAMPLES], heap[METRIC_SAMPLES], psram[METRIC_SAMPLES], temp[METRIC_SAMPLES];
    taskENTER_CRITICAL(&metrics.mux); /* a copy, so the JSON is built outside the critical section */
    memcpy(cpu, metrics.cpu, sizeof(cpu));
    memcpy(heap, metrics.heap, sizeof(heap));
    memcpy(psram, metrics.psram, sizeof(psram));
    memcpy(temp, metrics.temp, sizeof(temp));
    taskEXIT_CRITICAL(&metrics.mux);
    db_json_t j;
    db_json_begin(&j, buf, cap);
    db_json_obj_open(&j, NULL);
    db_json_int(&j, "every_s", METRIC_EVERY_US / 1000000);
    write_series(&j, "cpu", cpu);
    write_series(&j, "heap", heap);
    write_series(&j, "psram", psram);
    write_series(&j, "temp", temp);
    write_flash(&j);
    db_json_obj_close(&j);
    return db_json_end(&j);
}

esp_err_t get_metrics(httpd_req_t *req)
{
    return send_built(req, 4096, metrics_json);
}

esp_err_t get_profile(httpd_req_t *req)
{
    return send_built(req, 8192, profile_json);
}

void metrics_start(void)
{
    temperature_sensor_config_t range = TEMPERATURE_SENSOR_CONFIG_DEFAULT(20, 100);
    if (temperature_sensor_install(&range, &tsens) == ESP_OK && temperature_sensor_enable(tsens) != ESP_OK) {
        temperature_sensor_uninstall(tsens);
        tsens = NULL; /* the page shows no temperature */
    }
    esp_timer_handle_t timer;
    esp_timer_create_args_t every = {.callback = sample_metrics, .name = "metrics"};
    if (esp_timer_create(&every, &timer) == ESP_OK) {
        esp_timer_start_periodic(timer, METRIC_EVERY_US);
    }
}

/* Profiling counters, once a second into the trace ring while it records. */
static void trace_sample(void)
{
    static const uint32_t caps[2] = {MALLOC_CAP_INTERNAL, MALLOC_CAP_SPIRAM};
    for (int k = 0; k < 2; k++) {
        DB_TRACE(DB_TP_COUNTER, DB_TC_HEAP_FREE + 3 * k, heap_caps_get_free_size(caps[k]), 0);
        DB_TRACE(DB_TP_COUNTER, DB_TC_HEAP_MIN + 3 * k, heap_caps_get_minimum_free_size(caps[k]), 0);
        DB_TRACE(DB_TP_COUNTER, DB_TC_HEAP_LARGEST + 3 * k, heap_caps_get_largest_free_block(caps[k]), 0);
    }
    db_net_status_t n;
    db_net_status(&n); /* its RSSI is at most 2 s old: each fresh one is a call to the WLAN chip */
    DB_TRACE(DB_TP_COUNTER, DB_TC_RSSI, n.wlan_up ? n.rssi + 128 : 0, 0);
    for (int c = DB_TC_LINK_FRAMES; c < DB_TC_COUNT; c++) {
        DB_TRACE(DB_TP_COUNTER, c, __atomic_load_n(&db_trace_counts[c], __ATOMIC_RELAXED), 0);
    }
}

static void trace_task(void *arg)
{
    for (;;) {
        vTaskDelay(pdMS_TO_TICKS(1000));
        if (db_trace_ring) {
            trace_sample();
        }
    }
}

static const char *wifi_ps(void)
{
    wifi_ps_type_t ps;
    if (esp_wifi_get_ps(&ps) != ESP_OK) { /* asks the WLAN chip itself */
        return "unknown";
    }
    return ps == WIFI_PS_NONE ? "none" : ps == WIFI_PS_MIN_MODEM ? "min_modem" : "max_modem";
}

/* Pairing codes only while a recording runs: bench runs pair without reading the deck. */
static esp_err_t trace_state(httpd_req_t *req)
{
    char out[200];
    snprintf(out, sizeof(out),
             "{\"on\":%s,\"written\":%lu,\"capacity\":%u,\"now_us\":%lld,\"wifi_ps\":\"%s\","
             "\"pair_codes\":[\"%s\",\"%s\"]}",
             db_trace_ring ? "true" : "false", (unsigned long)db_trace_written(), DB_TRACE_EVENTS,
             (long long)esp_timer_get_time(), wifi_ps(), db_trace_ring ? db_link_pair_code(0) : "",
             db_trace_ring ? db_link_pair_code(1) : "");
    return send_json(req, out);
}

/* {"on": true} starts a new recording, {"on": false} stops it; RAM only, so every boot starts with it off. */
esp_err_t post_trace(httpd_req_t *req)
{
    char body[64];
    if (read_body(req, body, sizeof(body)) < 0) {
        return send_error(req, "413 Payload Too Large", "body too large");
    }
    cJSON *root = cJSON_Parse(body);
    const cJSON *on = cJSON_GetObjectItemCaseSensitive(root, "on");
    bool valid = cJSON_IsBool(on);
    bool want = cJSON_IsTrue(on);
    cJSON_Delete(root);
    if (!valid) {
        return send_error(req, "400 Bad Request", "on: true or false expected");
    }
    static TaskHandle_t sampler;
    if (want && !sampler && xTaskCreatePinnedToCore(trace_task, "trace", 3072, NULL, 2, &sampler, 1) != pdPASS) {
        return send_error(req, "500 Internal Server Error", "no memory");
    }
    if (want && !db_trace_on()) {
        return send_error(req, "500 Internal Server Error", "no memory for the ring");
    }
    if (!want) {
        db_trace_off();
    }
    return trace_state(req);
}

/* The state as JSON, or with ?events the kept events, oldest first, as 16-byte records (db_trace_ev_t). */
esp_err_t get_trace(httpd_req_t *req)
{
    char q[16];
    if (httpd_req_get_url_query_str(req, q, sizeof(q)) != ESP_OK || strcmp(q, "events") != 0) {
        return trace_state(req);
    }
    enum { CHUNK = 256 };
    db_trace_ev_t *buf = malloc(CHUNK * sizeof(*buf));
    if (!buf) {
        return send_error(req, "500 Internal Server Error", "no memory");
    }
    char written[12];
    snprintf(written, sizeof(written), "%lu", (unsigned long)db_trace_written());
    httpd_resp_set_type(req, "application/octet-stream");
    httpd_resp_set_hdr(req, "X-Trace-Written", written);
    esp_err_t err = ESP_OK;
    size_t n;
    for (uint32_t from = 0; err == ESP_OK && (n = db_trace_copy(from, buf, CHUNK)) > 0; from += n) {
        err = httpd_resp_send_chunk(req, (const char *)buf, (ssize_t)(n * sizeof(*buf)));
    }
    free(buf);
    return err == ESP_OK ? httpd_resp_send_chunk(req, NULL, 0) : err;
}

static int64_t bench_us(void)
{
    return esp_timer_get_time();
}

/* MB/s of `reps` copies of `len` bytes from `src` to `dst`. */
static double copy_rate(uint8_t *dst, const uint8_t *src, size_t len, int reps)
{
    int64_t t = bench_us();
    for (int i = 0; i < reps; i++) {
        memcpy(dst, src, len);
    }
    int64_t us = bench_us() - t;
    vTaskDelay(1); /* the idle task feeds the watchdog */
    return us ? (double)len * reps / us : 0;
}

static void bench_copies(db_json_t *j)
{
    enum { BIG = 1024 * 1024, SMALL = 64 * 1024, INNER = 16 * 1024 };
    uint8_t *a = heap_caps_malloc(BIG, MALLOC_CAP_SPIRAM);
    uint8_t *b = heap_caps_malloc(BIG, MALLOC_CAP_SPIRAM);
    uint8_t *in = heap_caps_malloc(INNER, MALLOC_CAP_INTERNAL);
    db_json_obj_open(j, "memcpy_mb_s");
    if (a && b && in) {
        memset(a, 0x5a, BIG);
        db_json_int(j, "psram_psram_1m", (int64_t)copy_rate(b, a, BIG, 8));
        db_json_int(j, "psram_psram_64k", (int64_t)copy_rate(b, a, SMALL, 64));
        db_json_int(j, "psram_internal_16k", (int64_t)copy_rate(in, a, INNER, 128));
        db_json_int(j, "internal_psram_16k", (int64_t)copy_rate(b, in, INNER, 128));
    }
    db_json_obj_close(j);
    heap_caps_free(a);
    heap_caps_free(b);
    heap_caps_free(in);
}

static void bench_jpeg(db_json_t *j)
{
    static const uint16_t sizes[][2] = {{72, 72}, {120, 120}, {800, 100}, {800, 480}};
    enum { REPS = 10 };
    db_json_arr_open(j, "jpeg");
    for (size_t i = 0; i < sizeof(sizes) / sizeof(sizes[0]); i++) {
        uint16_t w = sizes[i][0];
        uint16_t h = sizes[i][1];
        uint8_t *rgb = jpeg_port_alloc_rgb(w, h);
        uint8_t *back = jpeg_port_alloc_rgb(w, h);
        uint8_t *jpg = NULL;
        size_t len = 0;
        if (!rgb || !back) {
            heap_caps_free(rgb);
            heap_caps_free(back);
            continue;
        }
        for (size_t p = 0; p < (size_t)w * h * 3; p++) { /* gradients with some detail, like a key picture */
            rgb[p] = (uint8_t)((p / 3 % w) * 255 / w ^ (p / 3 / w) * 7);
        }
        int64_t enc = 0;
        int64_t dec = 0;
        esp_err_t err = ESP_OK;
        for (int r = 0; r < REPS && err == ESP_OK; r++) {
            heap_caps_free(jpg);
            jpg = NULL;
            int64_t t = bench_us();
            err = jpeg_port_encode(rgb, w, h, 90, &jpg, &len);
            enc += bench_us() - t;
            t = bench_us();
            err = err == ESP_OK ? jpeg_port_decode_into(jpg, len, back, w, h, 0, 0) : err;
            dec += bench_us() - t;
        }
        vTaskDelay(1);
        db_json_obj_open(j, NULL);
        db_json_int(j, "w", w);
        db_json_int(j, "h", h);
        if (err == ESP_OK) {
            db_json_int(j, "bytes", (int64_t)len);
            db_json_int(j, "encode_us", enc / REPS);
            db_json_int(j, "decode_us", dec / REPS);
        } else {
            db_json_str(j, "error", esp_err_to_name(err));
        }
        db_json_obj_close(j);
        heap_caps_free(jpg);
        heap_caps_free(rgb);
        heap_caps_free(back);
    }
    db_json_arr_close(j);
}

/* The ROM's miniz, which the Satellite PNGs go through. */
static void bench_inflate(db_json_t *j)
{
    enum { RAW = 256 * 1024, REPS = 4 };
    uint8_t *raw = heap_caps_malloc(RAW, MALLOC_CAP_SPIRAM);
    uint8_t *back = heap_caps_malloc(RAW, MALLOC_CAP_SPIRAM);
    uint8_t *z = heap_caps_malloc(RAW, MALLOC_CAP_SPIRAM);
    tinfl_decompressor *d = malloc(sizeof(*d));
    /* the ROM's miniz is built without malloc: its *_mem_to_heap helpers only return NULL */
    tdefl_compressor *c = heap_caps_malloc(sizeof(*c), MALLOC_CAP_SPIRAM);
    size_t zlen = 0;
    if (raw && back && z && d && c) {
        for (size_t p = 0; p < RAW; p++) { /* rows of a picture with flat areas: PNG-like */
            raw[p] = (uint8_t)((p % 800) / 50 * 16 + (p / 800 % 3));
        }
        size_t in_len = RAW;
        zlen = RAW;
        bool done = tdefl_init(c, NULL, NULL, TDEFL_WRITE_ZLIB_HEADER | 128) == TDEFL_STATUS_OKAY &&
                    tdefl_compress(c, raw, &in_len, z, &zlen, TDEFL_FINISH) == TDEFL_STATUS_DONE;
        zlen = done ? zlen : 0;
    }
    heap_caps_free(c);
    db_json_obj_open(j, "inflate");
    if (zlen) {
        int64_t us = 0;
        bool ok = true;
        for (int r = 0; r < REPS; r++) {
            size_t in_len = zlen;
            size_t out_len = RAW;
            tinfl_init(d);
            int64_t t = bench_us();
            tinfl_status st = tinfl_decompress(d, z, &in_len, back, back, &out_len,
                                               TINFL_FLAG_PARSE_ZLIB_HEADER | TINFL_FLAG_USING_NON_WRAPPING_OUTPUT_BUF);
            us += bench_us() - t;
            ok &= st == TINFL_STATUS_DONE && out_len == RAW;
            vTaskDelay(1);
        }
        db_json_int(j, "raw_bytes", RAW);
        db_json_int(j, "zlib_bytes", (int64_t)zlen);
        db_json_int(j, "mb_s", ok && us ? (int64_t)((double)RAW * REPS / us) : 0);
        db_json_bool(j, "ok", ok && memcmp(raw, back, RAW) == 0);
    }
    db_json_obj_close(j);
    heap_caps_free(z);
    heap_caps_free(raw);
    heap_caps_free(back);
    free(d);
}

static size_t bench_json(char *buf, size_t cap)
{
    enum { N = 20000 };
    db_json_t j;
    db_json_begin(&j, buf, cap);
    db_json_obj_open(&j, NULL);
    int64_t t = bench_us();
    for (int i = 0; i < N; i++) {
        DB_TRACE(DB_TP_BENCH, 0, i, 0);
    }
    db_json_int(&j, "trace_off_ns", (bench_us() - t) * 1000 / N);
    if (db_trace_on()) {
        t = bench_us();
        for (int i = 0; i < N; i++) {
            DB_TRACE(DB_TP_BENCH, 0, i, 0);
        }
        db_json_int(&j, "trace_on_ns", (bench_us() - t) * 1000 / N);
        db_trace_off();
    }
    volatile int64_t sink = 0;
    t = bench_us();
    for (int i = 0; i < N; i++) {
        sink += esp_timer_get_time();
    }
    db_json_int(&j, "esp_timer_ns", (bench_us() - t) * 1000 / N);
    vTaskDelay(1);
    bench_copies(&j);
    bench_jpeg(&j);
    bench_inflate(&j);
    db_json_obj_close(&j);
    return db_json_end(&j);
}

/* Microbenchmarks. The trace point test records into the ring itself: refused while a trace records, and the
 * last recording is gone after it. */
esp_err_t post_bench(httpd_req_t *req)
{
    if (db_trace_ring) {
        return send_error(req, "409 Conflict", "a trace is recording: stop it first");
    }
    return send_built(req, 2048, bench_json);
}
