#include "web.h"

#include <stdio.h>
#include <string.h>

#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_random.h"
#include "mbedtls/pk.h"
#include "mbedtls/x509_crt.h"
#include "nvs.h"
#include "psa/crypto.h"

#define PEM_MAX 1024

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

bool web_cert(const char *name, char **cert, char **key, char fp[96])
{
    *cert = heap_caps_calloc(1, PEM_MAX, MALLOC_CAP_SPIRAM);
    *key = heap_caps_calloc(1, PEM_MAX, MALLOC_CAP_SPIRAM);
    nvs_handle_t h;
    bool ok = *cert && *key && nvs_open("web", NVS_READWRITE, &h) == ESP_OK;
    if (ok) {
        size_t cl = PEM_MAX, kl = PEM_MAX;
        if (nvs_get_str(h, "cert", *cert, &cl) != ESP_OK || nvs_get_str(h, "key", *key, &kl) != ESP_OK) {
            uint32_t t = esp_log_timestamp();
            ok = make(name, *cert, *key);
            bool saved = ok && nvs_set_str(h, "cert", *cert) == ESP_OK && nvs_set_str(h, "key", *key) == ESP_OK &&
                         nvs_commit(h) == ESP_OK;
            ESP_LOGI(TAG, "new certificate in %lu ms: %s", (unsigned long)(esp_log_timestamp() - t), ok ? "ok" : "failed");
            if (ok && !saved) {
                ESP_LOGE(TAG, "certificate not saved: its fingerprint changes at the next restart");
            }
        }
        nvs_close(h);
    }
    if (ok) {
        fingerprint(*cert, fp);
    } else {
        heap_caps_free(*cert);
        heap_caps_free(*key);
        *cert = *key = NULL;
    }
    return ok;
}
