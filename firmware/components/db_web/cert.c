#include "web.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <time.h>

#include "auth.h"
#include "cJSON.h"
#include "db_cert.h"
#include "db_config.h"
#include "db_deck.h"
#include "db_json.h"
#include "db_ui.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_random.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "mbedtls/oid.h"
#include "mbedtls/pk.h"
#include "mbedtls/platform_util.h"
#include "mbedtls/x509_crt.h"
#include "nvs.h"
#include "psa/crypto.h"
#if !CONFIG_IDF_TARGET_LINUX
#include "mbedtls/ssl.h"
#endif

#define PEM_MAX 1536
#define DER_MAX 1024
/* An uploaded certificate may carry its chain. Together with the key it lives in the 24 KB NVS partition next
 * to the settings, which need room for a new copy each time they are saved. */
#define CUSTOM_MAX 6144
#define NVS_SPARE  4096
#define DAY        86400
#define LEAF_DAYS                                                                                            \
    800 /* Apple refuses a server certificate valid longer than 825 days, also a trusted one                 \
         */
#define SANS 2

static const char *TAG = "cert";

/* The certificate HTTPS serves. A new one goes into the other slot, so a handshake under way keeps its own.
 */
typedef struct {
    char *cert, *key; /* PEM */
    mbedtls_x509_crt crt;
    mbedtls_pk_context pk;
} pair_t;
static pair_t pairs[2];
static int live = -1;
static bool custom;
static bool custom_unread; /* an uploaded pair is kept but could not be read this boot */
static uint8_t *ca;        /* the device CA, DER */
static size_t ca_len;
static char ca_for[33];   /* the name the CA may vouch for */
static char ca_short[20]; /* "1A2B 3C4D 5E6F 7A8B": the start of its SHA-256, to compare with the deck */
static char names[80];    /* the live own certificate is for these, see db_cert_names() */
static int64_t until;
static char *fp_out;
static volatile bool renewing;
static int64_t next_try;

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

/* P-256: RSA-2048 made each handshake three times as slow. */
static bool new_key(mbedtls_pk_context *pk)
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
    bool ok = mbedtls_pk_copy_from_psa(id, pk) == 0;
    psa_destroy_key(id);
    return ok;
}

/* Before SNTP the clock counts from 1970: then the build day stands in. */
static int64_t now_s(void)
{
    static time_t built;
    if (!built) {
        static const char months[] = "JanFebMarAprMayJunJulAugSepOctNovDec";
        const char *d = __DATE__;
        const char *m = strstr(months, (char[4]){d[0], d[1], d[2], '\0'});
        struct tm b = {
            .tm_mday = atoi(d + 4), .tm_mon = m ? (int)(m - months) / 3 : 0, .tm_year = atoi(d + 7) - 1900};
        built = mktime(&b);
    }
    time_t t = time(NULL);
    return t > built ? t : built;
}

static void stamp(int64_t t, char out[16])
{
    time_t tt = (time_t)t;
    struct tm b;
    gmtime_r(&tt, &b);
    strftime(out, 16, "%Y%m%d%H%M%S", &b);
}

/* The device CA (`san` NULL), or a server certificate for the names in `san` signed by it. PEM, or DER at the
 * start of `buf` with its length in `*len`. */
static bool issue(mbedtls_pk_context *subject_key, mbedtls_pk_context *ca_key, const char *san, int64_t from,
                  bool pem, uint8_t *buf, size_t *len)
{
    /* The CA vouches only for this board's own name: its key, read out of a stolen board (NVS is not
     * encrypted), cannot pass for any other host to a device that trusts the CA. */
    uint8_t constraints[128];
    size_t constraints_len = db_cert_constraints(ca_for, constraints, sizeof(constraints));
    char issuer[64], subject[80], nb[16], na[16];
    uint8_t serial[16];
    esp_fill_random(serial, sizeof(serial));
    serial[0] &= 0x7f; /* a positive number */
    stamp(from, nb);
    stamp(san ? from + LEAF_DAYS * DAY : 2524607999 /* 2049-12-31 */, na);
    snprintf(issuer, sizeof(issuer), "CN=DeckConnector %s CA", ca_for);
    if (san) {
        snprintf(subject, sizeof(subject), "CN=%.*s", (int)strcspn(san, ","), san);
    } else {
        strlcpy(subject, issuer, sizeof(subject));
    }
    mbedtls_x509_san_list sans[SANS] = {0};
    const char *p = san;
    for (int n = 0; p && *p && n < SANS; n++) {
        size_t l = strcspn(p, ",");
        sans[n].node.type = MBEDTLS_X509_SAN_DNS_NAME;
        sans[n].node.san.unstructured_name = (mbedtls_x509_buf){.p = (unsigned char *)p, .len = l};
        if (n) {
            sans[n - 1].next = &sans[n];
        }
        p += l + (p[l] == ',');
    }
    /* serverAuth; mbedtls_x509write_crt_set_ext_key_usage() marks it critical, which verifiers that follow
     * the CA/Browser Forum refuse */
    static const uint8_t eku[] = {0x30, 0x0a, 0x06, 0x08, 0x2b, 0x06, 0x01, 0x05, 0x05, 0x07, 0x03, 0x01};
    mbedtls_x509write_cert crt;
    mbedtls_x509write_crt_init(&crt);
    mbedtls_x509write_crt_set_version(&crt, MBEDTLS_X509_CRT_VERSION_3);
    mbedtls_x509write_crt_set_md_alg(&crt, MBEDTLS_MD_SHA256);
    mbedtls_x509write_crt_set_subject_key(&crt, subject_key);
    mbedtls_x509write_crt_set_issuer_key(&crt, ca_key);
    bool ok = mbedtls_x509write_crt_set_subject_name(&crt, subject) == 0 &&
              mbedtls_x509write_crt_set_issuer_name(&crt, issuer) == 0 &&
              mbedtls_x509write_crt_set_serial_raw(&crt, serial, sizeof(serial)) == 0 &&
              mbedtls_x509write_crt_set_validity(&crt, nb, na) == 0 &&
              mbedtls_x509write_crt_set_subject_key_identifier(&crt) == 0;
    if (san) {
        ok = ok && mbedtls_x509write_crt_set_key_usage(&crt, MBEDTLS_X509_KU_DIGITAL_SIGNATURE) == 0 &&
             mbedtls_x509write_crt_set_extension(&crt, MBEDTLS_OID_EXTENDED_KEY_USAGE,
                                                 sizeof(MBEDTLS_OID_EXTENDED_KEY_USAGE) - 1, 0, eku,
                                                 sizeof(eku)) == 0 &&
             mbedtls_x509write_crt_set_subject_alternative_name(&crt, sans) == 0 &&
             mbedtls_x509write_crt_set_authority_key_identifier(&crt) == 0;
    } else {
        ok = ok && mbedtls_x509write_crt_set_basic_constraints(&crt, 1, 0) == 0 &&
             mbedtls_x509write_crt_set_key_usage(&crt, MBEDTLS_X509_KU_KEY_CERT_SIGN |
                                                           MBEDTLS_X509_KU_CRL_SIGN) == 0 &&
             mbedtls_x509write_crt_set_extension(&crt, MBEDTLS_OID_NAME_CONSTRAINTS,
                                                 sizeof(MBEDTLS_OID_NAME_CONSTRAINTS) - 1, 1, constraints,
                                                 constraints_len) == 0;
    }
    int r = !ok   ? -1
            : pem ? mbedtls_x509write_crt_pem(&crt, buf, *len)
                  : mbedtls_x509write_crt_der(&crt, buf, *len);
    mbedtls_x509write_crt_free(&crt);
    if (!pem && r > 0) {
        memmove(buf, buf + *len - (size_t)r, (size_t)r);
        *len = (size_t)r;
    }
    return pem ? r == 0 : r > 0;
}

static void drop(pair_t *p)
{
    heap_caps_free(p->cert);
    heap_caps_free(p->key);
    mbedtls_x509_crt_free(&p->crt);
    mbedtls_pk_free(&p->pk);
    *p = (pair_t){0};
    mbedtls_x509_crt_init(&p->crt);
    mbedtls_pk_init(&p->pk);
}

static bool parse(pair_t *p)
{
    return mbedtls_x509_crt_parse(&p->crt, (const unsigned char *)p->cert, strlen(p->cert) + 1) == 0 &&
           mbedtls_pk_parse_key(&p->pk, (const unsigned char *)p->key, strlen(p->key) + 1, NULL, 0) == 0;
}

static void go_live(pair_t *p)
{
    char fp[96];
    fingerprint(p->cert, fp);
    live = (int)(p - pairs);
    if (fp_out) {
        strcpy(fp_out, fp);
    }
}

/* The names the board answers to now. */
static void wanted(char *out, size_t cap)
{
    db_cert_names(db_config()->name, out, cap);
}

/* The device CA from NVS, made for the board's name. A new name, or a CA without a name from an earlier build
 * (its constraints let it vouch for every .local name and private address), gets a new one (`*fresh`). Its
 * key stays in NVS and is read only to sign. */
static bool load_ca(nvs_handle_t h, bool *fresh)
{
    ca = heap_caps_malloc(DER_MAX, MALLOC_CAP_SPIRAM);
    char *pem = heap_caps_calloc(1, PEM_MAX, MALLOC_CAP_SPIRAM);
    size_t len = DER_MAX, nl = sizeof(ca_for);
    esp_err_t err = ca && pem ? nvs_get_blob(h, "ca", ca, &len) : ESP_ERR_NO_MEM;
    esp_err_t named = err == ESP_OK ? nvs_get_str(h, "caname", ca_for, &nl) : err;
    bool ok = named == ESP_OK && strcmp(ca_for, db_config()->name) == 0;
    if (err == ESP_ERR_NVS_NOT_FOUND || named == ESP_ERR_NVS_NOT_FOUND || (named == ESP_OK && !ok)) {
        strlcpy(ca_for, db_config()->name, sizeof(ca_for));
        len = DER_MAX;
        mbedtls_pk_context pk;
        mbedtls_pk_init(&pk);
        ok = new_key(&pk) && issue(&pk, &pk, NULL, now_s() - DAY, false, ca, &len) &&
             mbedtls_pk_write_key_pem(&pk, (unsigned char *)pem, PEM_MAX) == 0 &&
             nvs_set_str(h, "cakey", pem) == ESP_OK && nvs_set_blob(h, "ca", ca, len) == ESP_OK &&
             nvs_set_str(h, "caname", ca_for) == ESP_OK && nvs_commit(h) == ESP_OK;
        mbedtls_pk_free(&pk);
        *fresh = true;
        ESP_LOGI(TAG, "new device CA, %u B stack left: %s", (unsigned)uxTaskGetStackHighWaterMark(NULL),
                 ok ? "ok" : "failed");
    } else if (!ok) {
        ESP_LOGE(TAG, "device CA unreadable (%s), no HTTPS until the next start", esp_err_to_name(err));
    }
    if (pem) {
        mbedtls_platform_zeroize(pem, PEM_MAX);
    }
    heap_caps_free(pem);
    ca_len = ok ? len : 0;
    uint8_t hash[32];
    size_t n;
    if (ok && psa_hash_compute(PSA_ALG_SHA_256, ca, ca_len, hash, sizeof(hash), &n) == PSA_SUCCESS) {
        snprintf(ca_short, sizeof(ca_short), "%02X%02X %02X%02X %02X%02X %02X%02X", hash[0], hash[1], hash[2],
                 hash[3], hash[4], hash[5], hash[6], hash[7]);
    }
    return ok;
}

/* A new key and server certificate for `want`, kept in NVS and served from the next handshake on. */
static bool renew(const char *want)
{
    pair_t *p = &pairs[live == 0];
    drop(p);
    uint32_t t = esp_log_timestamp();
    p->cert = heap_caps_calloc(1, PEM_MAX, MALLOC_CAP_SPIRAM);
    p->key = heap_caps_calloc(1, PEM_MAX, MALLOC_CAP_SPIRAM);
    char *ca_pem = heap_caps_calloc(1, PEM_MAX, MALLOC_CAP_SPIRAM);
    mbedtls_pk_context ca_key, key;
    mbedtls_pk_init(&ca_key);
    mbedtls_pk_init(&key);
    nvs_handle_t h = 0;
    size_t len = PEM_MAX;
    int64_t from = now_s() - DAY;
    bool ok = p->cert && p->key && ca_pem && nvs_open("web", NVS_READWRITE, &h) == ESP_OK &&
              nvs_get_str(h, "cakey", ca_pem, &len) == ESP_OK &&
              mbedtls_pk_parse_key(&ca_key, (const unsigned char *)ca_pem, len, NULL, 0) == 0 &&
              new_key(&key) &&
              issue(&key, &ca_key, want, from, true, (uint8_t *)p->cert, &(size_t){PEM_MAX}) &&
              mbedtls_pk_write_key_pem(&key, (unsigned char *)p->key, PEM_MAX) == 0 && parse(p);
    bool saved = ok && nvs_set_str(h, "cert", p->cert) == ESP_OK && nvs_set_str(h, "key", p->key) == ESP_OK &&
                 nvs_set_str(h, "san", want) == ESP_OK &&
                 nvs_set_i64(h, "until", from + LEAF_DAYS * DAY) == ESP_OK && nvs_commit(h) == ESP_OK;
    if (h) {
        nvs_close(h);
    }
    if (ca_pem) {
        mbedtls_platform_zeroize(ca_pem, PEM_MAX);
    }
    heap_caps_free(ca_pem);
    mbedtls_pk_free(&ca_key);
    mbedtls_pk_free(&key);
    ESP_LOGI(TAG, "new certificate for %s in %lu ms, %u B stack left: %s", want,
             (unsigned long)(esp_log_timestamp() - t), (unsigned)uxTaskGetStackHighWaterMark(NULL),
             ok ? "ok" : "failed");
    if (!ok) {
        drop(p);
        return false;
    }
    if (!saved) {
        ESP_LOGE(TAG, "certificate not saved: a new one comes at the next start");
    }
    strlcpy(names, want, sizeof(names));
    until = from + LEAF_DAYS * DAY;
    go_live(p);
    return true;
}

/* An uploaded pair from NVS "web" ccert/ckey, when it is there and still fits together. */
static bool load_custom(nvs_handle_t h, pair_t *p)
{
    size_t cl = 0, kl = 0;
    if (nvs_get_blob(h, "ccert", NULL, &cl) != ESP_OK || nvs_get_blob(h, "ckey", NULL, &kl) != ESP_OK ||
        cl + kl > CUSTOM_MAX) {
        return false;
    }
    p->cert = heap_caps_calloc(1, cl + 1, MALLOC_CAP_SPIRAM);
    p->key = heap_caps_calloc(1, kl + 1, MALLOC_CAP_SPIRAM);
    bool read = p->cert && p->key && nvs_get_blob(h, "ccert", p->cert, &cl) == ESP_OK &&
                nvs_get_blob(h, "ckey", p->key, &kl) == ESP_OK;
    char info[8];
    if (read && web_cert_check(p->cert, p->key, info, 0) && parse(p)) {
        return true;
    }
    drop(p);
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

/* The own certificate kept from the last start, or none: then names is empty. */
static void load_own(nvs_handle_t h, pair_t *p)
{
    p->cert = heap_caps_calloc(1, PEM_MAX, MALLOC_CAP_SPIRAM);
    p->key = heap_caps_calloc(1, PEM_MAX, MALLOC_CAP_SPIRAM);
    size_t cl = PEM_MAX, kl = PEM_MAX, sl = sizeof(names);
    if (!p->cert || !p->key || nvs_get_str(h, "cert", p->cert, &cl) != ESP_OK ||
        nvs_get_str(h, "key", p->key, &kl) != ESP_OK || nvs_get_str(h, "san", names, &sl) != ESP_OK ||
        nvs_get_i64(h, "until", &until) != ESP_OK || !parse(p)) {
        names[0] = '\0';
        drop(p);
    }
}

bool web_cert(char **cert, char **key, char fp[96])
{
    for (int i = 0; i < 2; i++) {
        drop(&pairs[i]);
    }
    fp_out = fp;
    nvs_handle_t h = 0;
    bool ok = nvs_open("web", NVS_READWRITE, &h) == ESP_OK;
    custom = ok && load_custom(h, &pairs[0]);
    bool fresh = false; /* a certificate kept from before was signed by another key */
    names[0] = '\0';
    ok = ok && (custom || load_ca(h, &fresh));
    if (ok && !custom && !fresh) {
        load_own(h, &pairs[0]);
    }
    if (h) {
        nvs_close(h);
    }
    char want[sizeof(names)];
    wanted(want, sizeof(want));
    if (custom || (ok && !db_cert_renew(names, want, now_s(), until))) {
        go_live(&pairs[0]);
    } else if (ok) {
        ok = renew(want);
    }
    *cert = ok ? pairs[live].cert : NULL;
    *key = ok ? pairs[live].key : NULL;
    return ok;
}

static void renew_task(void *arg)
{
    char want[sizeof(names)];
    wanted(want, sizeof(want));
    /* ponytail: one renewal per minute at most, so the slot it reuses served its last handshake long ago */
    next_try = esp_timer_get_time() + (renew(want) ? 60 : 3600) * 1000000LL;
    renewing = false;
    vTaskDelete(NULL);
}

void web_cert_tick(void)
{
    if (custom || live < 0 || renewing || esp_timer_get_time() < next_try) {
        return;
    }
    char want[sizeof(names)];
    wanted(want, sizeof(want));
    /* a new name waits for the restart that follows it: the CA is for the old one */
    if (strcasecmp(want, names) != 0 || !db_cert_renew(names, want, now_s(), until)) {
        return;
    }
    renewing = true;
    if (xTaskCreate(renew_task, "cert", 8192, NULL, 1, NULL) != pdPASS) {
        renewing = false;
    }
}

#if !CONFIG_IDF_TARGET_LINUX
int web_cert_select(mbedtls_ssl_context *ssl)
{
    pair_t *p = &pairs[live];
    return mbedtls_ssl_set_hs_own_cert(ssl, &p->crt, &p->pk);
}
#endif

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

const char *web_cert_ca_short(void)
{
    return custom ? "" : ca_short;
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

/* GET /api/cert: the certificate HTTPS serves, to pin. */
esp_err_t get_cert(httpd_req_t *req)
{
    if (live < 0) {
        return send_error(req, "404 Not Found", "HTTPS is not running");
    }
    httpd_resp_set_type(req, "application/x-pem-file");
    httpd_resp_set_hdr(req, "Content-Disposition", "attachment; filename=\"deckconnector.pem\"");
    return httpd_resp_send(req, pairs[live].cert, HTTPD_RESP_USE_STRLEN);
}

/* GET /api/ca: the device CA, to trust once. DER, which every system's certificate import takes. */
esp_err_t get_ca(httpd_req_t *req)
{
    if (!ca_len) {
        return send_error(req, "404 Not Found",
                          custom ? "an own certificate is in use" : "HTTPS is not running");
    }
    uint32_t who = auth_asker(req);
    if (db_deck_first() >= 0 &&
        !auth_paused_s(DB_UI_CA, who)) { /* the download goes on, the deck stays free */
        db_ui_show_ca(ca_short);
        auth_raised(DB_UI_CA, who);
    }
    httpd_resp_set_type(req, "application/x-x509-ca-cert");
    httpd_resp_set_hdr(req, "Content-Disposition", "attachment; filename=\"deckconnector-ca.crt\"");
    return httpd_resp_send(req, (const char *)ca, (ssize_t)ca_len);
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
    bool strings = cJSON_IsString(cert) && cJSON_IsString(key);
    bool sized = strings && strlen(cert->valuestring) + strlen(key->valuestring) <= CUSTOM_MAX;
    bool ok = sized && web_cert_check(cert->valuestring, key->valuestring, info, sizeof(info));
    esp_err_t stored = ok && apply ? web_cert_store(cert->valuestring, key->valuestring) : ESP_OK;
    cJSON_Delete(root);
    if (strings && !sized) {
        return send_error(req, "413 Payload Too Large", "certificate and key: at most 6144 bytes together");
    }
    if (!ok) {
        return send_error(req, "400 Bad Request",
                          "a PEM certificate and its PEM key (not encrypted) expected");
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
