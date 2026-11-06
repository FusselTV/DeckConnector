#include "db_ota.h"

#include <stdio.h>
#include <string.h>

#include "db_ptable.h"
#include "esp_app_desc.h"
#include "esp_flash.h"
#include "esp_private/esp_flash_internal.h"
#include "esp_flash_partitions.h"
#include "esp_image_format.h"
#include "esp_heap_caps.h"
#include "esp_hosted.h"
#include "esp_hosted_ota.h"
#include "esp_http_client.h"
#include "esp_log.h"
#include "esp_ota_ops.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "nvs.h"
#include "ota_pubkey.h"
#include "psa/crypto.h"

#define HEADER_LEN    144
#define SIGNED_LEN    76
#define C6_CHUNK      1400
#define TARGET_P4     1
#define TARGET_C6     2
#define TARGET_WEB    3
#define TARGET_BOOT   4 /* the bootloader at 0x2000 */
#define TARGET_TABLE  5 /* the partition table at 0x8000 */
#define CONFIRM_WAIT  120
#define CONFIRM_AFTER 20
/* Raise it with a security fix: files with a lower level are refused even when signed. dbfw.py reads it. */
#define SECURITY_LEVEL 1

static const char *TAG = "ota";

static struct {
    SemaphoreHandle_t lock;
    db_ota_progress_cb_t progress;
    bool busy;
    int slot;
    bool boot;
    uint16_t level;
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
    uint8_t *table; /* a new partition table, until it is checked */
    size_t c6_fill;
    char c6_version[16];
    bool restarts; /* the last finished update restarts the bridge */
} s;

static const char *target_name(void)
{
    return s.target == TARGET_C6     ? "c6"
           : s.target == TARGET_BOOT  ? "bootloader"
           : s.target == TARGET_TABLE ? "ptable"
                                      : "p4";
}

/* Not in our partition table, so known to the partition API only once registered here. */
static const esp_partition_t *primary(uint8_t target)
{
    bool boot = target == TARGET_BOOT;
    esp_partition_type_t type = boot ? ESP_PARTITION_TYPE_BOOTLOADER : ESP_PARTITION_TYPE_PARTITION_TABLE;
    const esp_partition_t *p = esp_partition_find_first(type, 0, NULL); /* subtype 0: the primary one */
    if (!p) {
        esp_partition_register_external(NULL, boot ? ESP_PRIMARY_BOOTLOADER_OFFSET : ESP_PRIMARY_PARTITION_TABLE_OFFSET,
                                        boot ? ESP_BOOTLOADER_SIZE : ESP_PARTITION_TABLE_SIZE,
                                        boot ? "PrimaryBTLDR" : "PrimaryPrtTable", type, 0, &p);
    }
    return p;
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
    s.lock = xSemaphoreCreateMutex(); /* here, at start: made on first use, two uploads at once both made one */
}

esp_err_t db_ota_begin(size_t total, int slot, bool boot)
{
    if (xSemaphoreTake(s.lock, 0) != pdTRUE) {
        return ESP_ERR_INVALID_STATE;
    }
    if (total <= HEADER_LEN) {
        xSemaphoreGive(s.lock);
        return ESP_ERR_INVALID_SIZE;
    }
    s.busy = true;
    s.slot = slot;
    s.boot = boot;
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
    if (s.target == TARGET_BOOT || s.target == TARGET_TABLE) {
        /* IDF opens both regions for writing and leaves them open, and only esp_ota_end() resets the offset */
        esp_flash_set_dangerous_write_protection(esp_flash_default_chip, true);
        esp_image_bootloader_offset_set(ESP_PRIMARY_BOOTLOADER_OFFSET);
    }
    heap_caps_free(s.c6_buf);
    s.c6_buf = NULL;
    heap_caps_free(s.table);
    s.table = NULL;
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

static bool signature_ok(const uint8_t *header)
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
    psa_status_t st =
        psa_verify_message(key, PSA_ALG_ECDSA(PSA_ALG_SHA_256), header, SIGNED_LEN, header + SIGNED_LEN, 64);
    psa_destroy_key(key);
    return st == PSA_SUCCESS;
}

static const esp_partition_t *slot_partition(int slot)
{
    return slot >= 0 && slot < 16 ? esp_partition_find_first(ESP_PARTITION_TYPE_APP,
                                                             ESP_PARTITION_SUBTYPE_APP_OTA_MIN + slot, NULL)
                                  : NULL;
}

/* A slot's image does not carry the security level of its .dbfw file, so it is noted here: at upload, and by
 * each firmware for its own slot when it runs. A slot without a note is never booted by choice. */
static void remember_level(const esp_partition_t *part, int level)
{
    nvs_handle_t h;
    if (nvs_open("ota", NVS_READWRITE, &h) == ESP_OK) {
        if (level < 0) {
            nvs_erase_key(h, part->label); /* the slot is about to be erased: its old note would lie */
        } else {
            nvs_set_u16(h, part->label, (uint16_t)level);
        }
        nvs_commit(h);
        nvs_close(h);
    }
}

static int known_level(const esp_partition_t *part)
{
    nvs_handle_t h;
    uint16_t level = 0;
    bool found = false;
    if (nvs_open("ota", NVS_READONLY, &h) == ESP_OK) {
        found = nvs_get_u16(h, part->label, &level) == ESP_OK;
        nvs_close(h);
    }
    return found ? level : -1;
}

/* Header layout: docs/ota.md. */
static esp_err_t start_payload(void)
{
    if (memcmp(s.header, "DBFW", 4) != 0 || s.header[4] != 1) {
        return ESP_ERR_INVALID_ARG;
    }
    s.target = s.header[5];
    memcpy(&s.payload_len, s.header + 8, 4);
    if (s.target != TARGET_P4 && s.target != TARGET_C6 && s.target != TARGET_BOOT && s.target != TARGET_TABLE) {
        return ESP_ERR_NOT_SUPPORTED;
    }
    if (!signature_ok(s.header)) {
        return ESP_ERR_INVALID_CRC;
    }
    memcpy(&s.level, s.header + 6, 2);
    if (s.level < SECURITY_LEVEL) {
        return ESP_ERR_INVALID_VERSION;
    }
    s.hash = psa_hash_operation_init();
    psa_hash_setup(&s.hash, PSA_ALG_SHA_256);
    ESP_LOGI(TAG, "signed %s image %.32s, %lu bytes", target_name(), (const char *)s.header + 12,
             (unsigned long)s.payload_len);
    if (s.target == TARGET_BOOT || s.target == TARGET_TABLE) {
        /* staged in the slot an update would use next, then IDF verifies it and copies it to its place */
        const esp_partition_t *final = primary(s.target);
        s.partition = esp_ota_get_next_update_partition(NULL);
        if (!final || !s.partition || s.partition == esp_ota_get_boot_partition() || s.payload_len > final->size) {
            return ESP_ERR_INVALID_SIZE;
        }
        /* kept until it is checked: a refused image must not erase the slot */
        s.table = heap_caps_malloc(final->size, MALLOC_CAP_SPIRAM);
        if (s.table) {
            memset(s.table, 0xff, final->size);
        }
        return s.table ? ESP_OK : ESP_ERR_NO_MEM;
    }
    if (s.target == TARGET_P4) {
        s.partition = s.slot < 0 ? esp_ota_get_next_update_partition(NULL) : slot_partition(s.slot);
        if (!s.partition || s.partition == esp_ota_get_running_partition()) {
            return ESP_ERR_NOT_FOUND;
        }
        int64_t t = esp_timer_get_time();
        remember_level(s.partition, -1);
        esp_err_t err = esp_ota_begin(s.partition, s.payload_len, &s.p4);
        ESP_LOGI(TAG, "slot %s erased in %lld ms", s.partition->label, (esp_timer_get_time() - t) / 1000);
        return err;
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
    esp_err_t err = ESP_OK;
    if (s.table) {
        memcpy(s.table + s.written, p, n);
    } else {
        err = s.target == TARGET_C6 ? write_c6(p, n) : esp_ota_write(s.p4, p, n);
    }
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

/* A new table must keep everything the running firmware and its data rely on (db_ptable.c). */
static esp_err_t check_table(const uint8_t *next)
{
    uint8_t *now = heap_caps_malloc(ESP_PARTITION_TABLE_SIZE, MALLOC_CAP_SPIRAM);
    const esp_partition_t *run = esp_ota_get_running_partition();
    esp_image_metadata_t meta = {0};
    esp_partition_pos_t pos = {.offset = run->address, .size = run->size};
    uint32_t flash = 0;
    esp_flash_get_size(NULL, &flash);
    int count = 0;
    const char *why = !now                                                               ? "memory"
                      : esp_partition_table_verify((const esp_partition_info_t *)next, false, &count) != ESP_OK
                          ? "not a valid table (MD5)"
                      : esp_partition_read(primary(TARGET_TABLE), 0, now, ESP_PARTITION_TABLE_SIZE) != ESP_OK
                          ? "read"
                      : esp_image_verify(ESP_IMAGE_VERIFY_SILENT, &pos, &meta) != ESP_OK ? "running image"
                          : db_ptable_check(now, next, ESP_PARTITION_TABLE_SIZE, run->address, meta.image_len, flash);
    heap_caps_free(now);
    if (why) {
        ESP_LOGE(TAG, "partition table refused: %s", why);
    }
    return why ? ESP_ERR_INVALID_ARG : ESP_OK;
}

/* Bootloader or table: checked, copied into place by esp_ota_end, then read back. */
static esp_err_t commit_boot(void)
{
    const esp_partition_t *final = primary(s.target);
    /* a bootloader: its image header (0xE9) and chip id here, IDF checks the rest in esp_ota_end, after staging
     * erased the slot and before the copy */
    uint16_t chip;
    memcpy(&chip, s.table + 12, 2);
    esp_err_t err = s.target == TARGET_TABLE                         ? check_table(s.table)
                    : s.table[0] == 0xE9 && chip == ESP_CHIP_ID_ESP32P4 ? ESP_OK
                                                                         : ESP_ERR_INVALID_ARG;
    if (err == ESP_OK) {
        remember_level(s.partition, -1);
        err = esp_ota_begin(s.partition, final->size, &s.p4); /* the whole size: the copy takes it all */
    }
    if (err == ESP_OK) {
        err = esp_ota_set_final_partition(s.p4, final, true);
    }
    if (err == ESP_OK) {
        err = esp_ota_write(s.p4, s.table, s.payload_len);
    }
    uint8_t *staged = heap_caps_malloc(final->size, MALLOC_CAP_SPIRAM);
    uint8_t *placed = heap_caps_malloc(s.payload_len, MALLOC_CAP_SPIRAM);
    if (err == ESP_OK) {
        err = staged && placed ? esp_partition_read(s.partition, 0, staged, final->size) : ESP_ERR_NO_MEM;
    }
    if (err == ESP_OK) {
        ESP_LOGW(TAG, "writing the %s", target_name());
        err = esp_ota_end(s.p4);
        s.p4 = 0;
        if (err == ESP_ERR_OTA_VALIDATE_FAILED) {
            ESP_LOGE(TAG, "%s refused by IDF before the copy", target_name());
        } else if (err != ESP_OK || esp_partition_read(final, 0, placed, s.payload_len) != ESP_OK ||
            memcmp(placed, staged, s.payload_len)) {
            /* the region may be half written: the board may not start again until it is written again */
            ESP_LOGE(TAG, "the %s at 0x%lx is written incompletely (%s): do not power off, write it again",
                     target_name(), (unsigned long)final->address, esp_err_to_name(err));
            err = DB_OTA_ERR_HALF_WRITTEN;
        }
    }
    heap_caps_free(staged);
    heap_caps_free(placed);
    return err;
}

static esp_err_t commit(void)
{
    if (s.target == TARGET_BOOT || s.target == TARGET_TABLE) {
        return commit_boot();
    }
    if (s.target == TARGET_P4) {
        esp_err_t err = esp_ota_end(s.p4);
        s.p4 = 0;
        if (err == ESP_OK) {
            remember_level(s.partition, s.level);
        }
        return err == ESP_OK && s.boot ? esp_ota_set_boot_partition(s.partition) : err;
    }
    if (s.c6_fill && esp_hosted_slave_ota_write(s.c6_buf, s.c6_fill) != ESP_OK) {
        return ESP_FAIL;
    }
    esp_err_t err = esp_hosted_slave_ota_end();
    /* Co-processor firmware before 2.6 switches over in end() and has no activate call. */
    esp_hosted_coprocessor_fwver_t v = {0};
    bool known = esp_hosted_get_coprocessor_fwversion(&v) == ESP_OK;
    if (err == ESP_OK && (!known || v.major1 > 2 || (v.major1 == 2 && v.minor1 >= 6))) {
        esp_err_t act = esp_hosted_slave_ota_activate();
        if (act != ESP_OK) {
            ESP_LOGW(TAG, "C6 activate: %s%s", esp_err_to_name(act), known ? "" : " (version unknown, maybe old firmware)");
            err = known ? act : ESP_OK;
        }
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
    bool restart = s.target == TARGET_C6 || (s.target == TARGET_P4 && s.boot);
    s.restarts = restart;
    ESP_LOGW(TAG, "%s update installed%s", target_name(), restart ? ", restarting" : "");
    report("done", 100, NULL);
    finish();
    /* After a C6 update the host restarts too, so both sides start their link again. */
    if (restart) {
        xTaskCreate(restart_task, "ota_restart", 2048, NULL, 5, NULL);
    }
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
    int status = err == ESP_OK ? esp_http_client_get_status_code(c) : 0;
    char why[32] = "no Content-Length";
    if (err != ESP_OK) {
        snprintf(why, sizeof(why), "%s", esp_err_to_name(err));
    } else if (status != 200) {
        snprintf(why, sizeof(why), "HTTP %d", status);
    }
    esp_err_t begun = err == ESP_OK && status == 200 && len > 0 ? db_ota_begin((size_t)len, -1, true) : ESP_FAIL;
    if (begun == ESP_ERR_INVALID_STATE) {
        ESP_LOGW(TAG, "download of %s skipped: an update is already running", url); /* its state stays as it is */
    } else if (begun != ESP_OK) {
        ESP_LOGE(TAG, "download of %s failed: %s", url, why);
        if (s.progress) {
            s.progress("unknown", "error", -1, why);
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
    bool reached = false;
    for (int i = 0; i < CONFIRM_WAIT; i++) {
        vTaskDelay(pdMS_TO_TICKS(1000));
        good = healthy() ? good + 1 : 0;
        if (good >= CONFIRM_AFTER) {
            reached = true;
            esp_err_t err = esp_ota_mark_app_valid_cancel_rollback();
            if (err == ESP_OK) {
                ESP_LOGI(TAG, "new firmware confirmed");
                vTaskDelete(NULL);
            }
            ESP_LOGE(TAG, "confirming the new firmware failed: %s, trying again", esp_err_to_name(err));
        }
    }
    ESP_LOGE(TAG, "%s, rolling back", reached ? "the healthy new firmware could not be confirmed"
                                                 : "new firmware never became healthy");
    esp_err_t err = esp_ota_mark_app_invalid_rollback_and_reboot();
    ESP_LOGE(TAG, "rolling back failed too: %s, the bootloader decides at the next start", esp_err_to_name(err));
    vTaskDelete(NULL);
}

static bool (*s_healthy)(void);

/* A restart asked for over the network before the 20 s are up would otherwise make the bootloader go back. */
static void confirm_on_restart(void)
{
    esp_ota_img_states_t state;
    if (s_healthy() && esp_ota_get_state_partition(esp_ota_get_running_partition(), &state) == ESP_OK &&
        state == ESP_OTA_IMG_PENDING_VERIFY) {
        esp_ota_mark_app_valid_cancel_rollback();
    }
}

void db_ota_confirm_when(bool (*healthy)(void))
{
    remember_level(esp_ota_get_running_partition(), SECURITY_LEVEL);
    esp_ota_img_states_t state;
    if (esp_ota_get_state_partition(esp_ota_get_running_partition(), &state) == ESP_OK &&
        state == ESP_OTA_IMG_PENDING_VERIFY) {
        ESP_LOGW(TAG, "running a new image, waiting for it to prove itself");
        s_healthy = healthy;
        esp_register_shutdown_handler(confirm_on_restart);
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

int db_ota_slots(db_ota_slot_t *out, int max)
{
    const esp_partition_t *running = esp_ota_get_running_partition();
    const esp_partition_t *next = esp_ota_get_boot_partition();
    int n = 0;
    for (const esp_partition_t *p; n < max && (p = slot_partition(n)); n++) {
        db_ota_slot_t *o = &out[n];
        memset(o, 0, sizeof(*o));
        snprintf(o->label, sizeof(o->label), "%s", p->label);
        o->running = p == running;
        o->next = p == next;
        o->level = known_level(p);
        esp_app_desc_t d;
        esp_ota_img_states_t st;
        if (esp_ota_get_partition_description(p, &d) != ESP_OK) {
            o->state = "empty";
            continue;
        }
        snprintf(o->version, sizeof(o->version), "%s", d.version);
        snprintf(o->built, sizeof(o->built), "%s %s", d.date, d.time);
        /* not in otadata (flashed over USB, or written before the last USB flash): only the running one is
         * known to work */
        o->state = esp_ota_get_state_partition(p, &st) != ESP_OK ? (o->running ? "valid" : "unknown")
                   : st == ESP_OTA_IMG_PENDING_VERIFY || st == ESP_OTA_IMG_NEW ? "pending"
                   : st == ESP_OTA_IMG_INVALID || st == ESP_OTA_IMG_ABORTED    ? "invalid"
                                                                               : "valid";
    }
    return n;
}

esp_err_t db_ota_boot_slot(int slot)
{
    const esp_partition_t *p = slot_partition(slot);
    esp_ota_img_states_t st;
    if (!p) {
        return ESP_ERR_NOT_FOUND;
    }
    /* The bootloader would abort an image left before it confirmed itself. */
    if (esp_ota_get_state_partition(esp_ota_get_running_partition(), &st) == ESP_OK &&
        st == ESP_OTA_IMG_PENDING_VERIFY) {
        return ESP_ERR_INVALID_STATE;
    }
    if (known_level(p) < SECURITY_LEVEL || (esp_ota_get_state_partition(p, &st) == ESP_OK &&
                                            (st == ESP_OTA_IMG_INVALID || st == ESP_OTA_IMG_ABORTED))) {
        return ESP_ERR_INVALID_VERSION;
    }
    esp_err_t err = esp_ota_set_boot_partition(p); /* checks the image; the new start must confirm itself */
    if (err == ESP_OK) {
        ESP_LOGW(TAG, "booting slot %s", p->label);
        xTaskCreate(restart_task, "ota_restart", 2048, NULL, 5, NULL);
    }
    return err;
}

/* The web page: a whole .dbfw image with target web, kept in PSRAM. Readers and db_ota_web_store run one at a
 * time (the web server's lock). */
static uint8_t *web_img;
static esp_err_t web_rejected; /* why the page in the partition is not served */

static const esp_partition_t *www(void)
{
    return esp_partition_find_first(ESP_PARTITION_TYPE_DATA, 0x40, "www");
}

static esp_err_t check_web(const uint8_t *img, size_t len, uint16_t api)
{
    uint32_t n;
    uint16_t api_min;
    if (len <= HEADER_LEN || memcmp(img, "DBFW", 4) != 0 || img[4] != 1 || img[5] != TARGET_WEB) {
        return ESP_ERR_INVALID_ARG;
    }
    memcpy(&n, img + 8, 4);
    memcpy(&api_min, img + 6, 2);
    uint8_t sha[32];
    size_t sha_len;
    if (n != len - HEADER_LEN) {
        return ESP_ERR_INVALID_SIZE;
    }
    if (!signature_ok(img) ||
        psa_hash_compute(PSA_ALG_SHA_256, img + HEADER_LEN, n, sha, sizeof(sha), &sha_len) != PSA_SUCCESS ||
        memcmp(sha, img + 44, sizeof(sha)) != 0) {
        return ESP_ERR_INVALID_CRC;
    }
    return api_min > api ? ESP_ERR_INVALID_VERSION : ESP_OK;
}

void db_ota_web_load(uint16_t api)
{
    const esp_partition_t *part = www();
    uint8_t head[HEADER_LEN];
    uint32_t n = 0;
    if (!part || esp_partition_read(part, 0, head, sizeof(head)) != ESP_OK || memcmp(head, "DBFW", 4) != 0) {
        return; /* empty: the page built into the firmware */
    }
    memcpy(&n, head + 8, 4);
    uint8_t *img = n <= part->size - HEADER_LEN ? heap_caps_malloc(HEADER_LEN + n, MALLOC_CAP_SPIRAM) : NULL;
    esp_err_t err = img ? esp_partition_read(part, 0, img, HEADER_LEN + n) : ESP_ERR_INVALID_SIZE;
    if (err == ESP_OK) {
        err = check_web(img, HEADER_LEN + n, api);
    }
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "web page in the partition not used: %s", esp_err_to_name(err));
        web_rejected = err;
        heap_caps_free(img);
        return;
    }
    web_img = img;
    ESP_LOGI(TAG, "web page %.32s from the partition", (const char *)img + 12);
}

esp_err_t db_ota_web_store(uint8_t *img, size_t len, uint16_t api)
{
    const esp_partition_t *part = www();
    esp_err_t err = part ? check_web(img, len, api) : ESP_ERR_NOT_FOUND;
    if (err == ESP_OK && len > part->size) {
        err = ESP_ERR_INVALID_SIZE;
    }
    if (err == ESP_OK) {
        err = esp_partition_erase_range(part, 0, (len + 4095) & ~4095u);
    }
    if (err == ESP_OK && esp_partition_write(part, 0, img, len) != ESP_OK) {
        esp_partition_erase_range(part, 0, 4096); /* no half page at the next start: the built-in one */
        err = ESP_FAIL;
    }
    if (err != ESP_OK) {
        return err;
    }
    heap_caps_free(web_img);
    web_img = img;
    web_rejected = ESP_OK;
    return ESP_OK;
}

esp_err_t db_ota_web_erase(void)
{
    const esp_partition_t *part = www();
    esp_err_t err = part ? esp_partition_erase_range(part, 0, 4096) : ESP_ERR_NOT_FOUND;
    if (err == ESP_OK) {
        heap_caps_free(web_img);
        web_img = NULL;
        web_rejected = ESP_OK;
    }
    return err;
}

esp_err_t db_ota_web_rejected(void)
{
    return web_rejected;
}

bool db_ota_web(db_ota_web_t *out)
{
    if (!web_img) {
        return false;
    }
    memcpy(&out->api_min, web_img + 6, 2);
    uint32_t n;
    memcpy(&n, web_img + 8, 4);
    out->len = n;
    out->gz = web_img + HEADER_LEN;
    out->sha = web_img + 44;
    out->version = (const char *)web_img + 12;
    return true;
}

/* Of the whole region as it is on the chip, so it matches GET /api/boot?raw=. */
static void sha_hex(uint8_t target, char *out)
{
    const esp_partition_t *p = primary(target);
    uint8_t *buf = p ? heap_caps_malloc(p->size, MALLOC_CAP_SPIRAM) : NULL;
    uint8_t sha[32];
    size_t n = 0;
    if (buf && esp_partition_read(p, 0, buf, p->size) == ESP_OK &&
        psa_hash_compute(PSA_ALG_SHA_256, buf, p->size, sha, sizeof(sha), &n) == PSA_SUCCESS) {
        for (int i = 0; i < 32; i++) {
            snprintf(out + 2 * i, 3, "%02x", sha[i]);
        }
    }
    heap_caps_free(buf);
}

void db_ota_boot_info(db_ota_boot_t *out)
{
    memset(out, 0, sizeof(*out));
    sha_hex(TARGET_BOOT, out->bootloader_sha256);
    sha_hex(TARGET_TABLE, out->table_sha256);
    esp_bootloader_desc_t d;
    if (esp_ota_get_bootloader_description(NULL, &d) == ESP_OK) {
        out->bootloader_version = d.version;
        snprintf(out->bootloader_idf, sizeof(out->bootloader_idf), "%.31s", d.idf_ver);
        snprintf(out->bootloader_built, sizeof(out->bootloader_built), "%.23s", d.date_time);
    }
    uint8_t e[32];
    const esp_partition_t *t = primary(TARGET_TABLE);
    for (size_t off = 0; t && off < ESP_PARTITION_TABLE_SIZE && esp_partition_read(t, off, e, sizeof(e)) == ESP_OK;
         off += sizeof(e)) {
        if (e[0] == 0xAA && e[1] == 0x50) {
            out->partitions++;
        } else {
            if (e[0] == 0xEB && e[1] == 0xEB) {
                for (int i = 0; i < 16; i++) {
                    snprintf(out->table_md5 + 2 * i, 3, "%02x", e[16 + i]);
                }
            }
            break;
        }
    }
}

size_t db_ota_boot_size(bool bootloader)
{
    const esp_partition_t *p = primary(bootloader ? TARGET_BOOT : TARGET_TABLE);
    return p ? p->size : 0;
}

esp_err_t db_ota_boot_read(bool bootloader, size_t offset, void *buf, size_t len)
{
    const esp_partition_t *p = primary(bootloader ? TARGET_BOOT : TARGET_TABLE);
    return p ? esp_partition_read(p, offset, buf, len) : ESP_ERR_NOT_FOUND;
}

bool db_ota_restarts(void)
{
    return s.restarts;
}
