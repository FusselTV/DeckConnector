#include <stdlib.h>

#include "db_json.h"
#include "driver/temperature_sensor.h"
#include "esp_heap_caps.h"
#include "esp_image_format.h"
#include "esp_ota_ops.h"
#include "esp_partition.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
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
    uint8_t temp[METRIC_SAMPLES];                                              /* the chip's, in °C */
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
    uint8_t temp = tsens && temperature_sensor_get_celsius(tsens, &c) == ESP_OK && c > 0 ? (uint8_t)(c + 0.5f) : 0;
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
    if (temperature_sensor_install(&range, &tsens) != ESP_OK || temperature_sensor_enable(tsens) != ESP_OK) {
        tsens = NULL; /* the page shows no temperature */
    }
    esp_timer_handle_t timer;
    esp_timer_create_args_t every = {.callback = sample_metrics, .name = "metrics"};
    if (esp_timer_create(&every, &timer) == ESP_OK) {
        esp_timer_start_periodic(timer, METRIC_EVERY_US);
    }
}
