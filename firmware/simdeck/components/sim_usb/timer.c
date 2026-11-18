/* esp_timer has only its header on Linux: one task runs the callbacks, as the chip's timer task does. */
#include <stdlib.h>
#include <time.h>

#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

struct esp_timer {
    esp_timer_cb_t cb;
    void *arg;
    int64_t due; /* 0: stopped */
    uint64_t period;
    struct esp_timer *next;
};

static struct esp_timer *timers;
static SemaphoreHandle_t lock;

int64_t esp_timer_get_time(void)
{
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return (int64_t)t.tv_sec * 1000000 + t.tv_nsec / 1000;
}

static void timer_task(void *arg)
{
    for (;;) {
        vTaskDelay(1);
        int64_t now = esp_timer_get_time();
        for (struct esp_timer *t = timers; t; t = t->next) {
            xSemaphoreTake(lock, portMAX_DELAY);
            bool due = t->due && t->due <= now;
            if (due) {
                t->due = t->period ? now + (int64_t)t->period : 0;
            }
            xSemaphoreGive(lock);
            if (due) {
                t->cb(t->arg);
            }
        }
    }
}

esp_err_t esp_timer_create(const esp_timer_create_args_t *args, esp_timer_handle_t *out)
{
    if (!lock) {
        lock = xSemaphoreCreateMutex();
        xTaskCreate(timer_task, "esp_timer", 8192, NULL, 22, NULL);
    }
    struct esp_timer *t = calloc(1, sizeof(*t));
    if (!t) {
        return ESP_ERR_NO_MEM;
    }
    t->cb = args->callback;
    t->arg = args->arg;
    xSemaphoreTake(lock, portMAX_DELAY);
    t->next = timers; /* never deleted: the firmware creates its timers once */
    timers = t;
    xSemaphoreGive(lock);
    *out = t;
    return ESP_OK;
}

static esp_err_t arm(esp_timer_handle_t t, uint64_t us, uint64_t period)
{
    if (t->due) {
        return ESP_ERR_INVALID_STATE; /* as IDF's: a running timer is stopped first */
    }
    xSemaphoreTake(lock, portMAX_DELAY);
    t->due = esp_timer_get_time() + (int64_t)us + 1;
    t->period = period;
    xSemaphoreGive(lock);
    return ESP_OK;
}

esp_err_t esp_timer_start_once(esp_timer_handle_t t, uint64_t us)
{
    return arm(t, us, 0);
}

esp_err_t esp_timer_start_periodic(esp_timer_handle_t t, uint64_t us)
{
    return arm(t, us, us);
}

esp_err_t esp_timer_stop(esp_timer_handle_t t)
{
    xSemaphoreTake(lock, portMAX_DELAY);
    esp_err_t err = t->due ? ESP_OK : ESP_ERR_INVALID_STATE;
    t->due = 0;
    xSemaphoreGive(lock);
    return err;
}

bool esp_timer_is_active(esp_timer_handle_t t)
{
    return t->due != 0;
}
