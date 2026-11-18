/* No flash partitions and no WLAN chip in the simulator: updates are refused, restarts are real. */
#include <stdint.h>
#include <string.h>

#include "db_ota.h"
#include "esp_system.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

esp_err_t db_ota_begin(size_t total, int slot, bool boot)
{
    return ESP_ERR_NOT_SUPPORTED;
}
esp_err_t db_ota_write(const uint8_t *data, size_t len)
{
    return ESP_ERR_INVALID_STATE;
}
esp_err_t db_ota_end(void)
{
    return ESP_ERR_INVALID_STATE;
}
void db_ota_abort(const char *why)
{
}
static void restart_task(void *arg)
{
    vTaskDelay(pdMS_TO_TICKS((uint32_t)(uintptr_t)arg));
    esp_restart();
}
void db_ota_restart(uint32_t delay_ms)
{
    xTaskCreate(restart_task, "restart", 4096, (void *)(uintptr_t)delay_ms, 5, NULL);
}
esp_err_t db_ota_start_url(const char *url)
{
    return ESP_ERR_NOT_SUPPORTED;
}
int db_ota_slots(db_ota_slot_t *out, int max)
{
    return 0;
}
esp_err_t db_ota_boot_slot(int slot)
{
    return ESP_ERR_NOT_SUPPORTED;
}
esp_err_t db_ota_slot_refusal(int slot)
{
    return ESP_ERR_NOT_FOUND;
}
void db_ota_web_load(uint16_t api)
{
}
esp_err_t db_ota_web_store(uint8_t *img, size_t len, uint16_t api)
{
    return ESP_ERR_NOT_SUPPORTED; /* no partition: the device tests cover it */
}
esp_err_t db_ota_web_erase(void)
{
    return ESP_OK;
}
bool db_ota_web(db_ota_web_t *out)
{
    return false;
}
esp_err_t db_ota_web_rejected(void)
{
    return ESP_OK;
}

const char *db_ota_refusal(void)
{
    return NULL;
}

bool db_ota_restarts(void)
{
    return false;
}

size_t db_ota_boot_size(bool bootloader)
{
    return 0;
}

esp_err_t db_ota_boot_read(bool bootloader, size_t offset, void *buf, size_t len)
{
    return ESP_ERR_NOT_FOUND;
}

void db_ota_boot_info(db_ota_boot_t *out)
{
    *out = (db_ota_boot_t){.partitions = 9};
}

const char *db_ota_inspect(const uint8_t *header)
{
    return memcmp(header, "DBFW", 4) == 0 ? "the simulator checks no signature" : "not a .dbfw file";
}

void db_ota_set_progress_cb(db_ota_progress_cb_t cb)
{
}

void db_ota_confirm_when(bool (*healthy)(void))
{
}

const char *db_ota_c6_version(void)
{
    return NULL;
}

esp_err_t db_ota_spare_erase(void)
{
    return ESP_OK;
}
