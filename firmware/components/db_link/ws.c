#include "ws.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "esp_crt_bundle.h"
#include "esp_log.h"
#include "esp_random.h"
#include "esp_timer.h"
#include "fcntl.h"
#include "lwip/netdb.h"
#include "lwip/sockets.h"
#include "mbedtls/base64.h"
#include "mbedtls/ssl.h"
#include "net/if.h"
#include "psa/crypto.h"

#define SEND_MAX     4096
#define RESPONSE_MAX 1024
#define WS_GUID      "258EAFA5-E914-47DA-95CA-C5AB0DC85B11"

static const char *TAG = "ws";

typedef struct {
    mbedtls_ssl_context ssl;
    mbedtls_ssl_config conf;
    int sock;
    const uint8_t *pin;
    bool pin_ok;
} tls_t;

int ws_init(ws_t *ws)
{
    ws->sock = -1;
    ws->tls = NULL;
    ws->tx = xSemaphoreCreateMutex();
    ws->frame = malloc(SEND_MAX + 8);
    if (!ws->tx || !ws->frame) {
        ws_deinit(ws);
        return -ENOMEM;
    }
    return 0;
}

void ws_deinit(ws_t *ws)
{
    if (ws->tx) {
        vSemaphoreDelete(ws->tx);
        ws->tx = NULL;
    }
    free(ws->frame);
    ws->frame = NULL;
}

static int parse_url(const char *url, char *host, size_t host_cap, unsigned *port, const char **path,
                     bool *tls)
{
    *tls = strncmp(url, "wss://", 6) == 0;
    if (!*tls && strncmp(url, "ws://", 5) != 0) {
        return -EPROTONOSUPPORT;
    }
    const char *h = url + (*tls ? 6 : 5);
    const char *end = h + strcspn(h, ":/");
    size_t n = (size_t)(end - h);
    if (n == 0 || n >= host_cap) {
        return -EINVAL;
    }
    memcpy(host, h, n);
    host[n] = '\0';
    *port = *tls ? 443 : 80;
    if (*end == ':') {
        *port = (unsigned)strtoul(end + 1, (char **)&end, 10);
    }
    *path = *end == '/' ? end : "/";
    return 0;
}

/* lwIP's blocking connect() ignores socket timeouts and retries SYNs for about 18 s on a dead link. */
static int connect_within(int s, const struct sockaddr *addr, socklen_t len, int timeout_ms)
{
    int flags = fcntl(s, F_GETFL, 0);
    fcntl(s, F_SETFL, flags | O_NONBLOCK);
    int err = 0;
    if (connect(s, addr, len) != 0) {
        if (errno != EINPROGRESS) {
            return -errno;
        }
        fd_set w;
        FD_ZERO(&w);
        FD_SET(s, &w);
        struct timeval tv = {.tv_sec = timeout_ms / 1000, .tv_usec = (timeout_ms % 1000) * 1000};
        if (select(s + 1, NULL, &w, NULL, &tv) <= 0) {
            return -ETIMEDOUT;
        }
        socklen_t el = sizeof(err);
        getsockopt(s, SOL_SOCKET, SO_ERROR, &err, &el);
        err = -err;
    }
    fcntl(s, F_SETFL, flags);
    return err;
}

int ws_tcp_open(const char *host, unsigned port, const char *ifname, int timeout_ms)
{
    char port_s[8];
    snprintf(port_s, sizeof(port_s), "%u", port);
    struct addrinfo hints = {.ai_family = AF_INET, .ai_socktype = SOCK_STREAM};
    struct addrinfo *res = NULL;
    if (getaddrinfo(host, port_s, &hints, &res) != 0 || !res) {
        return -EHOSTUNREACH;
    }
    int s = socket(AF_INET, SOCK_STREAM, 0);
    struct timeval tv = {.tv_sec = timeout_ms / 1000, .tv_usec = (timeout_ms % 1000) * 1000};
    setsockopt(s, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));
    setsockopt(s, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
    int one = 1;
    setsockopt(s, IPPROTO_TCP, TCP_NODELAY, &one, sizeof(one));
    if (ifname) {
        struct ifreq ifr = {0};
        strlcpy(ifr.ifr_name, ifname, sizeof(ifr.ifr_name));
        setsockopt(s, SOL_SOCKET, SO_BINDTODEVICE, &ifr, sizeof(ifr));
    }
    int err = connect_within(s, res->ai_addr, res->ai_addrlen, timeout_ms);
    freeaddrinfo(res);
    if (err) {
        close(s);
        return err;
    }
    return s;
}

int ws_url_host(const char *url, char *host, size_t host_cap, unsigned *port)
{
    const char *path;
    bool tls;
    return parse_url(url, host, host_cap, port, &path, &tls);
}

static int bio_send(void *ctx, const unsigned char *buf, size_t len)
{
    int n = send(((tls_t *)ctx)->sock, buf, len, 0);
    return n >= 0 ? n : (errno == EAGAIN || errno == EWOULDBLOCK ? MBEDTLS_ERR_SSL_WANT_WRITE : -1);
}

static int bio_recv(void *ctx, unsigned char *buf, size_t len)
{
    int n = recv(((tls_t *)ctx)->sock, buf, len, 0);
    return n >= 0 ? n : (errno == EAGAIN || errno == EWOULDBLOCK ? MBEDTLS_ERR_SSL_WANT_READ : -1);
}

/* With a pin the certificate itself is the trust: no CA, no name, only its SHA-256. */
static int check_pin(void *ctx, mbedtls_x509_crt *crt, int depth, uint32_t *flags)
{
    tls_t *t = ctx;
    if (depth == 0) {
        uint8_t h[32];
        size_t n = 0;
        t->pin_ok =
            psa_hash_compute(PSA_ALG_SHA_256, crt->raw.p, crt->raw.len, h, sizeof(h), &n) == PSA_SUCCESS &&
            memcmp(h, t->pin, sizeof(h)) == 0;
    }
    *flags = 0;
    return 0;
}

static void tls_free(tls_t *t)
{
    if (t) {
        mbedtls_ssl_free(&t->ssl);
        mbedtls_ssl_config_free(&t->conf);
        free(t);
    }
}

static int tls_start(ws_t *ws, int sock, const char *host, const uint8_t *pin, int timeout_ms)
{
    tls_t *t = calloc(1, sizeof(*t));
    if (!t || psa_crypto_init() != PSA_SUCCESS) {
        free(t);
        return -ENOMEM;
    }
    t->sock = sock;
    t->pin = pin;
    mbedtls_ssl_init(&t->ssl);
    mbedtls_ssl_config_init(&t->conf);
    int err = mbedtls_ssl_config_defaults(&t->conf, MBEDTLS_SSL_IS_CLIENT, MBEDTLS_SSL_TRANSPORT_STREAM,
                                          MBEDTLS_SSL_PRESET_DEFAULT);
    if (!err && pin) {
        /* OPTIONAL: REQUIRED insists on a CA chain; the pin is checked once the handshake is done */
        mbedtls_ssl_conf_authmode(&t->conf, MBEDTLS_SSL_VERIFY_OPTIONAL);
        mbedtls_ssl_conf_verify(&t->conf, check_pin, t);
    } else if (!err) {
        mbedtls_ssl_conf_authmode(&t->conf, MBEDTLS_SSL_VERIFY_REQUIRED);
        err = esp_crt_bundle_attach(&t->conf) == ESP_OK ? 0 : -1;
    }
    err = err ? err : mbedtls_ssl_setup(&t->ssl, &t->conf);
    err = err ? err : mbedtls_ssl_set_hostname(&t->ssl, host);
    mbedtls_ssl_set_bio(&t->ssl, t, bio_send, bio_recv, NULL);
    int64_t end = esp_timer_get_time() + (int64_t)timeout_ms * 1000;
    while (!err && (err = mbedtls_ssl_handshake(&t->ssl)) != 0) {
        if ((err == MBEDTLS_ERR_SSL_WANT_READ || err == MBEDTLS_ERR_SSL_WANT_WRITE) &&
            esp_timer_get_time() < end) {
            err = 0;
            continue;
        }
        break;
    }
    if (!err && pin && !t->pin_ok) {
        err = MBEDTLS_ERR_X509_CERT_VERIFY_FAILED;
    }
    if (err) {
        ESP_LOGW(TAG, "TLS with %s failed: -0x%04x%s", host, (unsigned)-err,
                 err == MBEDTLS_ERR_X509_CERT_VERIFY_FAILED ? ", certificate not trusted" : "");
        tls_free(t);
        return -ECONNREFUSED;
    }
    ws->tls = t;
    return 0;
}

/* recv() and send() on the socket, or through TLS. */
static int io_recv(ws_t *ws, int sock, void *buf, size_t len)
{
    tls_t *t = ws->tls;
    if (!t) {
        return recv(sock, buf, len, 0);
    }
    if (!mbedtls_ssl_get_bytes_avail(&t->ssl)) { /* wait outside the lock, so senders are not held up */
        struct timeval tv = {0};
        socklen_t tl = sizeof(tv);
        getsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, &tv, &tl);
        fd_set r;
        FD_ZERO(&r);
        FD_SET(sock, &r);
        if (select(sock + 1, &r, NULL, NULL, &tv) == 0) {
            errno = EAGAIN;
            return -1;
        }
    }
    xSemaphoreTake(ws->tx, portMAX_DELAY);
    int n = mbedtls_ssl_read(&t->ssl, buf, len);
    xSemaphoreGive(ws->tx);
    if (n == MBEDTLS_ERR_SSL_WANT_READ || n == MBEDTLS_ERR_SSL_WANT_WRITE) {
        errno = EAGAIN;
        return -1;
    }
    if (n == MBEDTLS_ERR_SSL_PEER_CLOSE_NOTIFY) {
        return 0;
    }
    if (n < 0) {
        errno = EIO;
        return -1;
    }
    return n;
}

/* Under `tx`, except while connecting. */
static int io_send(ws_t *ws, int sock, const void *buf, size_t len)
{
    tls_t *t = ws->tls;
    if (!t) {
        return send(sock, buf, len, 0);
    }
    size_t done = 0;
    while (done < len) {
        int n = mbedtls_ssl_write(&t->ssl, (const unsigned char *)buf + done, len - done);
        if (n == MBEDTLS_ERR_SSL_WANT_WRITE) { /* the socket blocks: this is the send timeout, the peer stalls */
            errno = EAGAIN;
            return -1;
        }
        if (n == MBEDTLS_ERR_SSL_WANT_READ) {
            continue;
        }
        if (n < 0) {
            errno = EIO;
            return -1;
        }
        done += (size_t)n;
    }
    return (int)len;
}

/* Sec-WebSocket-Accept is base64(SHA-1(key + GUID)). */
static bool accept_matches(const char *key, const char *response)
{
    char concat[96]; /* 24 byte key + 36 byte GUID */
    snprintf(concat, sizeof(concat), "%s%s", key, WS_GUID);
    uint8_t digest[20];
    size_t n = 0;
    if (psa_crypto_init() != PSA_SUCCESS ||
        psa_hash_compute(PSA_ALG_SHA_1, (const uint8_t *)concat, strlen(concat), digest, sizeof(digest),
                         &n) != PSA_SUCCESS) {
        return false;
    }
    unsigned char want[32];
    size_t wn = 0;
    mbedtls_base64_encode(want, sizeof(want) - 1, &wn, digest, sizeof(digest));
    want[wn] = '\0';
    const char *h = strcasestr(response, "\r\nSec-WebSocket-Accept:");
    if (!h) {
        return false;
    }
    h += strlen("\r\nSec-WebSocket-Accept:");
    while (*h == ' ') {
        h++;
    }
    return strncmp(h, (const char *)want, wn) == 0;
}

static int handshake(ws_t *ws, int s, const char *host, unsigned port, const char *path,
                     const char *subprotocol, const char *extra)
{
    uint8_t nonce[16];
    esp_fill_random(nonce, sizeof(nonce));
    char key[32];
    size_t kn = 0;
    mbedtls_base64_encode((unsigned char *)key, sizeof(key) - 1, &kn, nonce, sizeof(nonce));
    key[kn] = '\0';
    char req[512];
    char proto[64] = ""; /* Companion's Satellite API offers none, and an empty one is refused */
    if (subprotocol) {
        snprintf(proto, sizeof(proto), "Sec-WebSocket-Protocol: %s\r\n", subprotocol);
    }
    int n = snprintf(req, sizeof(req),
                     "GET %s HTTP/1.1\r\nHost: %s:%u\r\nUpgrade: websocket\r\nConnection: Upgrade\r\n"
                     "Sec-WebSocket-Key: %s\r\nSec-WebSocket-Version: 13\r\n%s%s\r\n",
                     path, host, port, key, proto, extra ? extra : "");
    if (n >= (int)sizeof(req) || io_send(ws, s, req, (size_t)n) != n) {
        return -EIO;
    }
    char resp[RESPONSE_MAX];
    size_t got = 0;
    /* Read byte by byte up to the blank line, so no frame bytes are swallowed. */
    while (got + 1 < sizeof(resp)) {
        if (io_recv(ws, s, resp + got, 1) != 1) {
            return -EIO;
        }
        got++;
        if (got >= 4 && memcmp(resp + got - 4, "\r\n\r\n", 4) == 0) {
            break;
        }
    }
    resp[got] = '\0';
    if (strncmp(resp, "HTTP/1.1 101", 12) != 0 || !accept_matches(key, resp)) {
        return -ECONNREFUSED;
    }
    return 0;
}

int ws_connect(ws_t *ws, const char *url, const char *subprotocol, const char *extra_headers,
               const char *ifname, int timeout_ms, const uint8_t *pin)
{
    char host[64];
    unsigned port;
    const char *path;
    bool tls;
    int err = parse_url(url, host, sizeof(host), &port, &path, &tls);
    if (err) {
        return err;
    }
    int s = ws_tcp_open(host, port, ifname, timeout_ms);
    if (s < 0) {
        return s;
    }
    err = tls ? tls_start(ws, s, host, pin, timeout_ms) : 0;
    err = err ? err : handshake(ws, s, host, port, path, subprotocol, extra_headers);
    if (err) {
        tls_free(ws->tls);
        ws->tls = NULL;
        close(s);
        return err;
    }
    /* Short read timeout from now on, so the reader can send pings and notice silent peers. */
    struct timeval tv = {.tv_sec = 1};
    setsockopt(s, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
    xSemaphoreTake(ws->tx, portMAX_DELAY);
    ws->sock = s;
    xSemaphoreGive(ws->tx);
    return 0;
}

int ws_probe(const char *url, const char *ifname, int timeout_ms)
{
    char host[64];
    unsigned port;
    const char *path;
    bool tls;
    int err = parse_url(url, host, sizeof(host), &port, &path, &tls);
    int s = err ? err : ws_tcp_open(host, port, ifname, timeout_ms);
    if (s < 0) {
        return s;
    }
    close(s);
    return 0;
}

void ws_close(ws_t *ws)
{
    xSemaphoreTake(ws->tx, portMAX_DELAY);
    if (ws->sock >= 0) {
        if (ws->tls) {
            mbedtls_ssl_close_notify(&((tls_t *)ws->tls)->ssl);
        }
        shutdown(ws->sock, SHUT_RDWR);
        close(ws->sock);
        ws->sock = -1;
    }
    tls_free(ws->tls);
    ws->tls = NULL;
    xSemaphoreGive(ws->tx);
}

/* Each recv() waits at most 1 s (SO_RCVTIMEO); a frame that stalls halfway gives up after STALL_LIMIT_S. */
#define STALL_LIMIT_S 6

static int read_exact(ws_t *ws, uint8_t *buf, size_t len, bool allow_idle)
{
    size_t got = 0;
    int stalls = 0;
    while (got < len) {
        int n = io_recv(ws, ws->sock, buf + got, len - got);
        if (n > 0) {
            got += (size_t)n;
            stalls = 0;
        } else if (n < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
            if (allow_idle && got == 0) {
                return -EAGAIN;
            }
            if (++stalls >= STALL_LIMIT_S) {
                return -ETIMEDOUT;
            }
        } else {
            return n == 0 ? -ECONNRESET : -errno;
        }
    }
    return 0;
}

int ws_read_header(ws_t *ws, uint8_t *op, bool *fin, uint64_t *len)
{
    uint8_t h[8];
    int err = read_exact(ws, h, 2, true);
    if (err) {
        return err;
    }
    *fin = h[0] & 0x80;
    *op = h[0] & 0x0f;
    if (h[1] & 0x80) {
        return -EPROTO; /* servers must not mask */
    }
    uint64_t n = h[1] & 0x7f;
    if (n == 126) {
        err = read_exact(ws, h, 2, false);
        n = ((uint64_t)h[0] << 8) | h[1];
    } else if (n == 127) {
        err = read_exact(ws, h, 8, false);
        n = 0;
        for (int i = 0; i < 8; i++) {
            n = (n << 8) | h[i];
        }
    }
    *len = n;
    return err;
}

int ws_read(ws_t *ws, void *buf, size_t len)
{
    return read_exact(ws, buf, len, false);
}

int ws_skip(ws_t *ws, uint64_t len)
{
    uint8_t buf[256];
    while (len > 0) {
        size_t n = len < sizeof(buf) ? (size_t)len : sizeof(buf);
        int err = read_exact(ws, buf, n, false);
        if (err) {
            return err;
        }
        len -= n;
    }
    return 0;
}

int ws_send(ws_t *ws, uint8_t op, const void *data, size_t len)
{
    if (len > SEND_MAX) {
        return -EMSGSIZE;
    }
    uint8_t *frame = ws->frame;
    xSemaphoreTake(ws->tx, portMAX_DELAY);
    int err = -ENOTCONN;
    if (ws->sock >= 0) {
        size_t h = 2;
        frame[0] = (uint8_t)(0x80 | op);
        if (len < 126) {
            frame[1] = (uint8_t)(0x80 | len);
        } else {
            frame[1] = 0x80 | 126;
            frame[2] = (uint8_t)(len >> 8);
            frame[3] = (uint8_t)len;
            h = 4;
        }
        uint8_t *mask = frame + h;
        esp_fill_random(mask, 4);
        const uint8_t *p = data;
        for (size_t i = 0; i < len; i++) {
            frame[h + 4 + i] = p[i] ^ mask[i & 3];
        }
        size_t total = h + 4 + len;
        /* One send() per frame keeps header and payload in one TCP segment. */
        err = io_send(ws, ws->sock, frame, total) == (int)total ? 0 : -errno;
        if (err) {
            /* A partly sent frame breaks the stream; end the session so the reader reconnects. */
            shutdown(ws->sock, SHUT_RDWR);
        }
    }
    xSemaphoreGive(ws->tx);
    return err;
}
