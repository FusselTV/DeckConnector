#include "web.h"

#include <stdio.h>
#include <string.h>

#include "esp_heap_caps.h"
#include "cJSON.h"
#include "db_json.h"
#include "esp_log.h"
#include "esp_random.h"
#include "mbedtls/pk.h"
#include "mbedtls/x509_crt.h"
#include "nvs.h"
#include "psa/crypto.h"

#define PEM_MAX    1024
/* An uploaded certificate may carry its chain. Together with the key it lives in the 24 KB NVS partition next
 * to the settings, which need room for a new copy each time they are saved. */
#define CUSTOM_MAX  6144
#define NVS_SPARE   4096

static const char *TAG = "cert";

/* A new P-256 key and a certificate for it, signed by itself: RSA-2048 made each handshake three times as slow. */
static bool make(const char *name, char *cert, char *key)
{
    psa_key_attributes_t attr = PSA_KEY_ATTRIBUTES_INIT;
    psa_set_key_type(&attr, PSA_KEY_TYPE_ECC_KEY_PAIR(PSA_ECC_FAMILY_SECP_R1));
    psa_set_key_bits(&attr, 256);
    psa_set_key_usage_flags(&attr, PSA_KEY_USAGE_SIGN_HASH | PSA_KEY_USAGE_EXPORT);
    psa_set_key_algorithm(&attr, PSA_ALG_ECDSA(PSA_ALG_SHA_256));
    psa_key_id_t id;
    if (psa_crypto_init() != PSA_SUCCESS || psa_generate_key(&attr, &id) != PSA_SUCCESS) {
        return false;
    }
    mbedtls_pk_context pk;
    mbedtls_pk_init(&pk);
    mbedtls_x509write_cert crt;
    mbedtls_x509write_crt_init(&crt);
    char subject[48];
    snprintf(subject, sizeof(subject), "CN=%s", name);
    uint8_t serial[16];
    esp_fill_random(serial, sizeof(serial));
    serial[0] &= 0x7f; /* a positive number */
    mbedtls_x509_san_list san = {.node = {.type = MBEDTLS_X509_SAN_DNS_NAME}};
    char dns[48];
    snprintf(dns, sizeof(dns), "%s.local", name);
    san.node.san.unstructured_name = (mbedtls_x509_buf){.p = (unsigned char *)dns, .len = strlen(dns)};
    bool ok = mbedtls_pk_copy_from_psa(id, &pk) == 0;
    mbedtls_x509write_crt_set_version(&crt, MBEDTLS_X509_CRT_VERSION_3);
    mbedtls_x509write_crt_set_md_alg(&crt, MBEDTLS_MD_SHA256);
    mbedtls_x509write_crt_set_subject_key(&crt, &pk);
    mbedtls_x509write_crt_set_issuer_key(&crt, &pk);
    ok = ok && mbedtls_x509write_crt_set_subject_name(&crt, subject) == 0 &&
         mbedtls_x509write_crt_set_issuer_name(&crt, subject) == 0 &&
         mbedtls_x509write_crt_set_serial_raw(&crt, serial, sizeof(serial)) == 0 &&
         mbedtls_x509write_crt_set_validity(&crt, "20260101000000", "20491231235959") == 0 &&
         mbedtls_x509write_crt_set_subject_alternative_name(&crt, &san) == 0 &&
         mbedtls_x509write_crt_pem(&crt, (unsigned char *)cert, PEM_MAX) == 0 &&
         mbedtls_pk_write_key_pem(&pk, (unsigned char *)key, PEM_MAX) == 0;
    mbedtls_x509write_crt_free(&crt);
    mbedtls_pk_free(&pk);
    psa_destroy_key(id);
    return ok;
}

static void fingerprint(const char *cert, char out[96])
{
    mbedtls_x509_crt crt;
    mbedtls_x509_crt_init(&crt);
    uint8_t hash[32];
    size_t n = 0;
    out[0] = '\0';
    if (mbedtls_x509_crt_parse(&crt, (const unsigned char *)cert, strlen(cert) + 1) == 0 &&
        psa_hash_compute(PSA_ALG_SHA_256, crt.raw.p, crt.raw.len, hash, sizeof(hash), &n) == PSA_SUCCESS) {
        for (size_t i = 0; i < n; i++) {
            sprintf(out + 3 * i, i + 1 < n ? "%02X:" : "%02X", hash[i]);
        }
    }
    mbedtls_x509_crt_free(&crt);
}

static const char *active; /* the certificate HTTPS serves, for GET /api/cert */
static bool custom;
static bool custom_unread; /* an uploaded pair is kept but could not be read this boot */

/* An uploaded pair from NVS "web" ccert/ckey, when it is there and still fits together. */
static bool load_custom(nvs_handle_t h, char **cert, char **key)
{
    size_t cl = 0, kl = 0;
    if (nvs_get_blob(h, "ccert", NULL, &cl) != ESP_OK || nvs_get_blob(h, "ckey", NULL, &kl) != ESP_OK ||
        cl + kl > CUSTOM_MAX) {
        return false;
    }
    char *c = heap_caps_calloc(1, cl + 1, MALLOC_CAP_SPIRAM);
    char *k = heap_caps_calloc(1, kl + 1, MALLOC_CAP_SPIRAM);
    bool read = c && k && nvs_get_blob(h, "ccert", c, &cl) == ESP_OK && nvs_get_blob(h, "ckey", k, &kl) == ESP_OK;
    char info[8];
    if (read && web_cert_check(c, k, info, 0)) {
        *cert = c;
        *key = k;
        return true;
    }
    heap_caps_free(c);
    heap_caps_free(k);
    if (!read) {
        ESP_LOGE(TAG, "the uploaded certificate could not be read, using the own one until the next start");
        custom_unread = true;
        return false;
    }
    ESP_LOGE(TAG, "the uploaded certificate no longer loads, using the own one");
    nvs_erase_key(h, "ccert"); /* its NVS space goes back to the settings */
    nvs_erase_key(h, "ckey");
    nvs_commit(h);
    return false;
}

bool web_cert(const char *name, char **cert, char **key, char fp[96])
{
    nvs_handle_t h = 0;
    bool ok = nvs_open("web", NVS_READWRITE, &h) == ESP_OK;
    if (ok && load_custom(h, cert, key)) {
        nvs_close(h);
        custom = true;
        active = *cert;
        fingerprint(*cert, fp);
        return true;
    }
    *cert = heap_caps_calloc(1, PEM_MAX, MALLOC_CAP_SPIRAM);
    *key = heap_caps_calloc(1, PEM_MAX, MALLOC_CAP_SPIRAM);
    ok = ok && *cert && *key;
    if (ok) {
        size_t cl = PEM_MAX, kl = PEM_MAX;
        esp_err_t ce = nvs_get_str(h, "cert", *cert, &cl),
                  ke = ce == ESP_OK ? nvs_get_str(h, "key", *key, &kl) : ce;
        if (ke != ESP_OK && ke != ESP_ERR_NVS_NOT_FOUND) {
            ESP_LOGE(TAG, "own certificate unreadable (%s), no HTTPS until the next start",
                     esp_err_to_name(ke));
            ok = false;
        } else if (ke == ESP_ERR_NVS_NOT_FOUND) {
            uint32_t t = esp_log_timestamp();
            ok = make(name, *cert, *key);
            bool saved = ok && nvs_set_str(h, "cert", *cert) == ESP_OK && nvs_set_str(h, "key", *key) == ESP_OK &&
                         nvs_commit(h) == ESP_OK;
            ESP_LOGI(TAG, "new certificate in %lu ms: %s", (unsigned long)(esp_log_timestamp() - t), ok ? "ok" : "failed");
            if (ok && !saved) {
                ESP_LOGE(TAG, "certificate not saved: its fingerprint changes at the next restart");
            }
        }
    }
    if (h) {
        nvs_close(h);
    }
    if (ok) {
        active = *cert;
        fingerprint(*cert, fp);
    } else {
        heap_caps_free(*cert);
        heap_caps_free(*key);
        *cert = *key = NULL;
    }
    return ok;
}

bool web_cert_check(const char *cert, const char *key, char *info, size_t cap)
{
    mbedtls_x509_crt crt;
    mbedtls_pk_context pk;
    mbedtls_x509_crt_init(&crt);
    mbedtls_pk_init(&pk);
    bool ok = mbedtls_x509_crt_parse(&crt, (const unsigned char *)cert, strlen(cert) + 1) == 0 &&
              mbedtls_pk_parse_key(&pk, (const unsigned char *)key, strlen(key) + 1, NULL, 0) == 0 &&
              mbedtls_pk_check_pair(&crt.pk, &pk) == 0;
    if (ok && cap) {
        char subject[96] = "", issuer[96] = "", fp[96];
        mbedtls_x509_dn_gets(subject, sizeof(subject), &crt.subject);
        mbedtls_x509_dn_gets(issuer, sizeof(issuer), &crt.issuer);
        fingerprint(cert, fp);
        const mbedtls_x509_time *t = &crt.valid_to;
        char date[16];
        snprintf(date, sizeof(date), "%04d-%02d-%02d", t->year, t->mon, t->day);
        db_json_t j;
        db_json_begin(&j, info, cap);
        db_json_obj_open(&j, NULL);
        db_json_bool(&j, "ok", true);
        db_json_str(&j, "subject", subject); /* mbedtls escapes "," in a name with a backslash */
        db_json_str(&j, "issuer", issuer);
        db_json_str(&j, "not_after", date);
        db_json_str(&j, "sha256", fp);
        db_json_obj_close(&j);
        db_json_end(&j);
    }
    mbedtls_pk_free(&pk);
    mbedtls_x509_crt_free(&crt);
    return ok;
}

bool web_cert_custom(void)
{
    return custom;
}

bool web_cert_custom_unread(void)
{
    return custom_unread;
}

esp_err_t web_cert_store(const char *cert, const char *key)
{
    nvs_handle_t h;
    esp_err_t err = nvs_open("web", NVS_READWRITE, &h);
    if (err != ESP_OK) {
        return err;
    }
    nvs_stats_t st;
    if (cert && (nvs_get_stats(NULL, &st) != ESP_OK ||
                 st.free_entries * 32 < strlen(cert) + strlen(key) + NVS_SPARE)) {
        err = ESP_ERR_NVS_NOT_ENOUGH_SPACE;
    } else if (cert) {
        err = nvs_set_blob(h, "ccert", cert, strlen(cert));
        if (err == ESP_OK && (err = nvs_set_blob(h, "ckey", key, strlen(key))) != ESP_OK) {
            nvs_erase_key(h, "ccert"); /* never a new certificate next to the old key */
            nvs_erase_key(h, "ckey");
        }
    } else {
        nvs_erase_key(h, "ccert");
        nvs_erase_key(h, "ckey");
    }
    esp_err_t committed = nvs_commit(h);
    err = err == ESP_OK ? committed : err;
    nvs_close(h);
    return err;
}

/* GET /api/cert: the public certificate HTTPS serves, to import into a trust store or to pin. */
esp_err_t get_cert(httpd_req_t *req)
{
    if (!active) {
        return send_error(req, "404 Not Found", "HTTPS is not running");
    }
    httpd_resp_set_type(req, "application/x-pem-file");
    httpd_resp_set_hdr(req, "Content-Disposition", "attachment; filename=\"deckconnector.pem\"");
    return httpd_resp_send(req, active, HTTPD_RESP_USE_STRLEN);
}

/* PUT /api/tls {cert, key}: check an uploaded pair; with "apply": true keep it and restart. */
esp_err_t put_tls(httpd_req_t *req)
{
    char *body = heap_caps_malloc(2 * CUSTOM_MAX + 256, MALLOC_CAP_SPIRAM);
    cJSON *root = body && read_body(req, body, 2 * CUSTOM_MAX + 256) >= 0 ? cJSON_Parse(body) : NULL;
    heap_caps_free(body);
    const cJSON *cert = cJSON_GetObjectItemCaseSensitive(root, "cert");
    const cJSON *key = cJSON_GetObjectItemCaseSensitive(root, "key");
    bool apply = cJSON_IsTrue(cJSON_GetObjectItemCaseSensitive(root, "apply"));
    char info[640]; /* names with escapes */
    bool sized = cJSON_IsString(cert) && cJSON_IsString(key) &&
                 strlen(cert->valuestring) + strlen(key->valuestring) <= CUSTOM_MAX;
    bool ok = sized && web_cert_check(cert->valuestring, key->valuestring, info, sizeof(info));
    esp_err_t stored = ok && apply ? web_cert_store(cert->valuestring, key->valuestring) : ESP_OK;
    bool parsed = root != NULL;
    cJSON_Delete(root);
    if (!sized && parsed) {
        return send_error(req, "413 Payload Too Large", "certificate and key: at most 6144 bytes together");
    }
    if (!ok) {
        return send_error(req, "400 Bad Request", "a PEM certificate and its PEM key (not encrypted) expected");
    }
    if (stored == ESP_ERR_NVS_NOT_ENOUGH_SPACE) {
        return send_error(req, "507 Insufficient Storage", "not saved: too little room next to the settings");
    }
    if (stored != ESP_OK) {
        return send_error(req, "500 Internal Server Error", "not saved");
    }
    send_json(req, info);
    if (apply) {
        restart_soon();
    }
    return ESP_OK;
}

/* DELETE /api/tls: back to the own certificate. */
esp_err_t delete_tls(httpd_req_t *req)
{
    if (web_cert_store(NULL, NULL) != ESP_OK) {
        return send_error(req, "500 Internal Server Error", "not saved");
    }
    send_json(req, "{\"ok\":true,\"restart\":true}");
    restart_soon();
    return ESP_OK;
}
