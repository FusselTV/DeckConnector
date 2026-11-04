#include "ws.h"

#include <assert.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "esp_random.h"
#include "lwip/netdb.h"
#include "lwip/sockets.h"
#include "mbedtls/base64.h"
#include "net/if.h"
#include "psa/crypto.h"

#define SEND_MAX     4096
#define RESPONSE_MAX 1024
#define WS_GUID      "258EAFA5-E914-47DA-95CA-C5AB0DC85B11"

void ws_init(ws_t *ws)
{
    ws->sock = -1;
    ws->tx = xSemaphoreCreateMutex();
    ws->frame = malloc(SEND_MAX + 8);
    assert(ws->frame); /* boot time, internal RAM */
}

static int parse_url(const char *url, char *host, size_t host_cap, unsigned *port, const char **path)
{
    if (strncmp(url, "ws://", 5) != 0) {
        return -EPROTONOSUPPORT;
    }
    const char *h = url + 5;
    const char *end = h + strcspn(h, ":/");
    size_t n = (size_t)(end - h);
    if (n == 0 || n >= host_cap) {
        return -EINVAL;
    }
    memcpy(host, h, n);
    host[n] = '\0';
    *port = 80;
    if (*end == ':') {
        *port = (unsigned)strtoul(end + 1, (char **)&end, 10);
    }
    *path = *end == '/' ? end : "/";
    return 0;
}

static int open_socket(const char *host, unsigned port, const char *ifname, int timeout_ms)
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
    int err = connect(s, res->ai_addr, res->ai_addrlen) == 0 ? 0 : -errno;
    freeaddrinfo(res);
    if (err) {
        close(s);
        return err;
    }
    return s;
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

static int handshake(int s, const char *host, unsigned port, const char *path, const char *subprotocol,
                     const char *extra)
{
    uint8_t nonce[16];
    esp_fill_random(nonce, sizeof(nonce));
    char key[32];
    size_t kn = 0;
    mbedtls_base64_encode((unsigned char *)key, sizeof(key) - 1, &kn, nonce, sizeof(nonce));
    key[kn] = '\0';
    char req[512];
    int n =
        snprintf(req, sizeof(req),
                 "GET %s HTTP/1.1\r\nHost: %s:%u\r\nUpgrade: websocket\r\nConnection: Upgrade\r\n"
                 "Sec-WebSocket-Key: %s\r\nSec-WebSocket-Version: 13\r\nSec-WebSocket-Protocol: %s\r\n%s\r\n",
                 path, host, port, key, subprotocol, extra ? extra : "");
    if (n >= (int)sizeof(req) || send(s, req, (size_t)n, 0) != n) {
        return -EIO;
    }
    char resp[RESPONSE_MAX];
    size_t got = 0;
    /* Read byte by byte up to the blank line, so no frame bytes are swallowed. */
    while (got + 1 < sizeof(resp)) {
        if (recv(s, resp + got, 1, 0) != 1) {
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
               const char *ifname, int timeout_ms)
{
    char host[64];
    unsigned port;
    const char *path;
    int err = parse_url(url, host, sizeof(host), &port, &path);
    if (err) {
        return err;
    }
    int s = open_socket(host, port, ifname, timeout_ms);
    if (s < 0) {
        return s;
    }
    err = handshake(s, host, port, path, subprotocol, extra_headers);
    if (err) {
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

void ws_close(ws_t *ws)
{
    xSemaphoreTake(ws->tx, portMAX_DELAY);
    if (ws->sock >= 0) {
        shutdown(ws->sock, SHUT_RDWR);
        close(ws->sock);
        ws->sock = -1;
    }
    xSemaphoreGive(ws->tx);
}

/* Each recv() waits at most 1 s (SO_RCVTIMEO); a frame that stalls halfway gives up after STALL_LIMIT_S. */
#define STALL_LIMIT_S 10

static int read_exact(int s, uint8_t *buf, size_t len, bool allow_idle)
{
    size_t got = 0;
    int stalls = 0;
    while (got < len) {
        int n = recv(s, buf + got, len - got, 0);
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
    int err = read_exact(ws->sock, h, 2, true);
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
        err = read_exact(ws->sock, h, 2, false);
        n = ((uint64_t)h[0] << 8) | h[1];
    } else if (n == 127) {
        err = read_exact(ws->sock, h, 8, false);
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
    return read_exact(ws->sock, buf, len, false);
}

int ws_skip(ws_t *ws, uint64_t len)
{
    uint8_t buf[256];
    while (len > 0) {
        size_t n = len < sizeof(buf) ? (size_t)len : sizeof(buf);
        int err = read_exact(ws->sock, buf, n, false);
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
        err = send(ws->sock, frame, total, 0) == (ssize_t)total ? 0 : -errno;
        if (err) {
            /* A partly sent frame breaks the stream; end the session so the reader reconnects. */
            shutdown(ws->sock, SHUT_RDWR);
        }
    }
    xSemaphoreGive(ws->tx);
    return err;
}
