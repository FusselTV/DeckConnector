#include "db_ota.h"

#include <stdio.h>
#include <string.h>

#include "esp_app_desc.h"
#include "esp_heap_caps.h"
#include "esp_hosted.h"
#include "esp_hosted_ota.h"
#include "esp_http_client.h"
#include "esp_log.h"
#include "esp_ota_ops.h"
#include "esp_system.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "ota_pubkey.h"
#include "psa/crypto.h"

#define HEADER_LEN    144
#define SIGNED_LEN    76
#define C6_CHUNK      1400
#define TARGET_P4     1
#define TARGET_C6     2
#define CONFIRM_WAIT  120
#define CONFIRM_AFTER 20
/* Raise it with a security fix: files with a lower level are refused even when signed. dbfw.py reads it. */
#define SECURITY_LEVEL 1

static const char *TAG = "ota";

static struct {
    SemaphoreHandle_t lock;
    db_ota_progress_cb_t progress;
    bool busy;
    uint8_t header[HEADER_LEN];
    size_t header_got;
    uint8_t target;
    uint32_t payload_len;
    uint32_t written;
    int last_pct;
    psa_hash_operation_t hash;
    esp_ota_handle_t p4;
    const esp_partition_t *partition;
    uint8_t *c6_buf;
    size_t c6_fill;
    char c6_version[16];
} s;

static const char *target_name(void)
{
    return s.target == TARGET_C6 ? "c6" : "p4";
}

static void report(const char *state, int pct, const char *error)
{
    if (s.progress) {
        s.progress(target_name(), state, pct, error);
    }
}

void db_ota_set_progress_cb(db_ota_progress_cb_t cb)
{
    s.progress = cb;
}

esp_err_t db_ota_begin(size_t total)
{
    if (!s.lock) {
        s.lock = xSemaphoreCreateMutex();
    }
    if (xSemaphoreTake(s.lock, 0) != pdTRUE) {
        return ESP_ERR_INVALID_STATE;
    }
    if (total <= HEADER_LEN) {
        xSemaphoreGive(s.lock);
        return ESP_ERR_INVALID_SIZE;
    }
    s.busy = true;
    s.header_got = 0;
    s.written = 0;
    s.last_pct = -1;
    s.target = 0;
    return ESP_OK;
}

static void finish(void)
{
    psa_hash_abort(&s.hash);
    if (s.p4) {
        esp_ota_abort(s.p4);
        s.p4 = 0;
    }
    heap_caps_free(s.c6_buf);
    s.c6_buf = NULL;
    s.busy = false;
    xSemaphoreGive(s.lock);
}

void db_ota_abort(const char *why)
{
    if (!s.busy) {
        return;
    }
    ESP_LOGE(TAG, "update aborted: %s", why);
    report("error", -1, why);
    finish();
}

static bool signature_ok(void)
{
    if (psa_crypto_init() != PSA_SUCCESS) {
        return false;
    }
    psa_key_attributes_t attr = PSA_KEY_ATTRIBUTES_INIT;
    psa_set_key_type(&attr, PSA_KEY_TYPE_ECC_PUBLIC_KEY(PSA_ECC_FAMILY_SECP_R1));
    psa_set_key_bits(&attr, 256);
    psa_set_key_usage_flags(&attr, PSA_KEY_USAGE_VERIFY_MESSAGE);
    psa_set_key_algorithm(&attr, PSA_ALG_ECDSA(PSA_ALG_SHA_256));
    psa_key_id_t key;
    if (psa_import_key(&attr, db_ota_pubkey, sizeof(db_ota_pubkey), &key) != PSA_SUCCESS) {
        return false;
    }
    psa_status_t st = psa_verify_message(key, PSA_ALG_ECDSA(PSA_ALG_SHA_256), s.header, SIGNED_LEN,
                                         s.header + SIGNED_LEN, 64);
    psa_destroy_key(key);
    return st == PSA_SUCCESS;
}

/* Header layout: docs/ota.md. */
static esp_err_t start_payload(void)
{
    if (memcmp(s.header, "DBFW", 4) != 0 || s.header[4] != 1) {
        return ESP_ERR_INVALID_ARG;
    }
    s.target = s.header[5];
    memcpy(&s.payload_len, s.header + 8, 4);
    if (s.target != TARGET_P4 && s.target != TARGET_C6) {
        return ESP_ERR_NOT_SUPPORTED;
    }
    if (!signature_ok()) {
        return ESP_ERR_INVALID_CRC;
    }
    uint16_t level;
    memcpy(&level, s.header + 6, 2);
    if (level < SECURITY_LEVEL) {
        return ESP_ERR_INVALID_VERSION;
    }
    s.hash = psa_hash_operation_init();
    psa_hash_setup(&s.hash, PSA_ALG_SHA_256);
    ESP_LOGI(TAG, "signed %s image %.32s, %lu bytes", target_name(), (const char *)s.header + 12,
             (unsigned long)s.payload_len);
    if (s.target == TARGET_P4) {
        s.partition = esp_ota_get_next_update_partition(NULL);
        return s.partition ? esp_ota_begin(s.partition, s.payload_len, &s.p4) : ESP_ERR_NOT_FOUND;
    }
    s.c6_buf = heap_caps_malloc(C6_CHUNK, MALLOC_CAP_INTERNAL);
    s.c6_fill = 0;
    return s.c6_buf ? esp_hosted_slave_ota_begin() : ESP_ERR_NO_MEM;
}

static esp_err_t write_c6(const uint8_t *p, size_t n)
{
    while (n > 0) {
        size_t take = C6_CHUNK - s.c6_fill < n ? C6_CHUNK - s.c6_fill : n;
        memcpy(s.c6_buf + s.c6_fill, p, take);
        s.c6_fill += take;
        p += take;
        n -= take;
        if (s.c6_fill == C6_CHUNK) {
            esp_err_t err = esp_hosted_slave_ota_write(s.c6_buf, C6_CHUNK);
            s.c6_fill = 0;
            if (err != ESP_OK) {
                return err;
            }
        }
    }
    return ESP_OK;
}

static esp_err_t write_payload(const uint8_t *p, size_t n)
{
    if (s.written + n > s.payload_len) {
        return ESP_ERR_INVALID_SIZE;
    }
    psa_hash_update(&s.hash, p, n);
    esp_err_t err = s.target == TARGET_P4 ? esp_ota_write(s.p4, p, n) : write_c6(p, n);
    s.written += (uint32_t)n;
    int pct = (int)((uint64_t)s.written * 100 / s.payload_len);
    if (pct / 5 != s.last_pct / 5) {
        s.last_pct = pct;
        report("download", pct, NULL);
    }
    return err;
}

esp_err_t db_ota_write(const uint8_t *data, size_t len)
{
    if (!s.busy) {
        return ESP_ERR_INVALID_STATE;
    }
    if (s.header_got < HEADER_LEN) {
        size_t take = HEADER_LEN - s.header_got < len ? HEADER_LEN - s.header_got : len;
        memcpy(s.header + s.header_got, data, take);
        s.header_got += take;
        data += take;
        len -= take;
        if (s.header_got == HEADER_LEN) {
            esp_err_t err = start_payload();
            if (err != ESP_OK) {
                db_ota_abort(err == ESP_ERR_INVALID_CRC       ? "signature"
                             : err == ESP_ERR_INVALID_VERSION ? "older than the security level"
                                                              : "header");
                return err;
            }
        }
    }
    esp_err_t err = len ? write_payload(data, len) : ESP_OK;
    if (err != ESP_OK) {
        db_ota_abort(esp_err_to_name(err));
    }
    return err;
}

static void restart_task(void *arg)
{
    vTaskDelay(pdMS_TO_TICKS(1500));
    esp_restart();
}

static esp_err_t commit(void)
{
    if (s.target == TARGET_P4) {
        esp_err_t err = esp_ota_end(s.p4);
        s.p4 = 0;
        return err == ESP_OK ? esp_ota_set_boot_partition(s.partition) : err;
    }
    if (s.c6_fill && esp_hosted_slave_ota_write(s.c6_buf, s.c6_fill) != ESP_OK) {
        return ESP_FAIL;
    }
    esp_err_t err = esp_hosted_slave_ota_end();
    /* Co-processor firmware before 2.6 switches over in end() and has no activate call. */
    esp_hosted_coprocessor_fwver_t v = {0};
    bool has_activate = esp_hosted_get_coprocessor_fwversion(&v) == ESP_OK &&
                        (v.major1 > 2 || (v.major1 == 2 && v.minor1 >= 6));
    if (err == ESP_OK && has_activate) {
        err = esp_hosted_slave_ota_activate();
    }
    return err;
}

esp_err_t db_ota_end(void)
{
    if (!s.busy) {
        return ESP_ERR_INVALID_STATE;
    }
    uint8_t digest[32];
    size_t n = 0;
    report("verify", 100, NULL);
    if (s.header_got < HEADER_LEN || s.written != s.payload_len ||
        psa_hash_finish(&s.hash, digest, sizeof(digest), &n) != PSA_SUCCESS ||
        memcmp(digest, s.header + 44, 32)) {
        db_ota_abort("hash");
        return ESP_ERR_INVALID_CRC;
    }
    esp_err_t err = commit();
    if (err != ESP_OK) {
        db_ota_abort(esp_err_to_name(err));
        return err;
    }
    ESP_LOGW(TAG, "%s update installed, restarting", target_name());
    report("done", 100, NULL);
    finish();
    /* After a C6 update the host restarts too, so both sides start their link again. */
    xTaskCreate(restart_task, "ota_restart", 2048, NULL, 5, NULL);
    return ESP_OK;
}

static void url_task(void *arg)
{
    char *url = arg;
    esp_http_client_config_t cfg = {.url = url, .timeout_ms = 10000, .buffer_size = 4096};
    esp_http_client_handle_t c = esp_http_client_init(&cfg);
    uint8_t *buf = heap_caps_malloc(4096, MALLOC_CAP_INTERNAL);
    esp_err_t err = buf && c ? esp_http_client_open(c, 0) : ESP_ERR_NO_MEM;
    int64_t len = err == ESP_OK ? esp_http_client_fetch_headers(c) : -1;
    if (len <= 0 || esp_http_client_get_status_code(c) != 200 || db_ota_begin((size_t)len) != ESP_OK) {
        ESP_LOGE(TAG, "download of %s failed (status %d)", url, esp_http_client_get_status_code(c));
        if (s.progress) {
            s.progress("unknown", "error", -1, "download");
        }
    } else {
        int n;
        int64_t got = 0;
        while (s.busy && (n = esp_http_client_read(c, (char *)buf, 4096)) > 0) {
            got += n;
            if (db_ota_write(buf, (size_t)n) != ESP_OK) {
                break;
            }
        }
        if (s.busy && got < len) {
            db_ota_abort("download interrupted");
        } else if (s.busy) {
            db_ota_end();
        }
    }
    heap_caps_free(buf);
    esp_http_client_cleanup(c);
    free(url);
    vTaskDelete(NULL);
}

esp_err_t db_ota_start_url(const char *url)
{
    if (s.busy) {
        return ESP_ERR_INVALID_STATE;
    }
    char *copy = strdup(url);
    if (!copy) {
        return ESP_ERR_NO_MEM;
    }
    ESP_LOGI(TAG, "downloading %s", url);
    return xTaskCreate(url_task, "ota_url", 6144, copy, 5, NULL) == pdPASS ? ESP_OK : ESP_ERR_NO_MEM;
}

static void confirm_task(void *arg)
{
    bool (*healthy)(void) = arg;
    int good = 0;
    for (int i = 0; i < CONFIRM_WAIT; i++) {
        vTaskDelay(pdMS_TO_TICKS(1000));
        good = healthy() ? good + 1 : 0;
        if (good >= CONFIRM_AFTER) {
            ESP_LOGI(TAG, "new firmware confirmed");
            esp_ota_mark_app_valid_cancel_rollback();
            vTaskDelete(NULL);
        }
    }
    ESP_LOGE(TAG, "new firmware never became healthy, rolling back");
    esp_ota_mark_app_invalid_rollback_and_reboot();
}

void db_ota_confirm_when(bool (*healthy)(void))
{
    esp_ota_img_states_t state;
    if (esp_ota_get_state_partition(esp_ota_get_running_partition(), &state) == ESP_OK &&
        state == ESP_OTA_IMG_PENDING_VERIFY) {
        ESP_LOGW(TAG, "running a new image, waiting for it to prove itself");
        xTaskCreate(confirm_task, "ota_confirm", 3072, healthy, 2, NULL);
    }
}

const char *db_ota_c6_version(void)
{
    if (!s.c6_version[0]) {
        esp_hosted_coprocessor_fwver_t v = {0};
        if (esp_hosted_get_coprocessor_fwversion(&v) == ESP_OK) {
            snprintf(s.c6_version, sizeof(s.c6_version), "%lu.%lu.%lu", (unsigned long)v.major1,
                     (unsigned long)v.minor1, (unsigned long)v.patch1);
        }
    }
    return s.c6_version[0] ? s.c6_version : NULL;
}
