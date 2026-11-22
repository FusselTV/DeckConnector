#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

/*
 * Minimal RFC 6455 client on a plain socket. Reading happens in one task; sending is safe from any task.
 * esp_websocket_client is not used: it passes data through an event loop and two locks, which capped
 * downloads at about 0.5 MB/s and hid the socket, so Nagle could not be turned off.
 */

enum {
    WS_OP_CONT = 0x0,
    WS_OP_TEXT = 0x1,
    WS_OP_BINARY = 0x2,
    WS_OP_CLOSE = 0x8,
    WS_OP_PING = 0x9,
    WS_OP_PONG = 0xa,
};

typedef struct {
    int sock;
    SemaphoreHandle_t tx; /* sending, and with TLS also reading: one TLS session is not safe from two tasks */
    uint8_t *frame;       /* send buffer, guarded by `tx` */
    void *tls;            /* NULL for ws:// */
    const char *fail; /* after a failed ws_connect that got past TCP: TLS, the certificate or the upgrade */
    bool any_cert;    /* wss:// without a pin takes any certificate, not only one a public CA signed */
    uint8_t cert[32]; /* SHA-256 of the server's certificate after a wss:// connect with a pin or any_cert */
    uint32_t peer;    /* the server's IPv4 address, network order */
} ws_t;

/* 0 or -ENOMEM. */
int ws_init(ws_t *ws);
/* After ws_close, for a ws_t that is not used again. */
void ws_deinit(ws_t *ws);
/*
 * `extra_headers` is a list of "Name: value\r\n" lines or NULL. `ifname` forces the interface (lwIP name),
 * NULL uses the routing table. A wss:// server must show the certificate whose SHA-256 is `pin`, or with no
 * pin one that a public CA signed for its name. Returns 0 or a negative errno.
 */
int ws_connect(ws_t *ws, const char *url, const char *subprotocol, const char *extra_headers,
               const char *ifname, int timeout_ms, const uint8_t *pin);
/* Host and port of a ws:// or wss:// URL. */
int ws_url_host(const char *url, char *host, size_t host_cap, unsigned *port);
void ws_close(ws_t *ws);
/* A plain TCP socket with TCP_NODELAY and `timeout_ms` for every send and receive, or a negative errno. */
int ws_tcp_open(const char *host, unsigned port, const char *ifname, int timeout_ms);
/* 0 when a TCP connection to the server in `url` opens over `ifname`; it is closed again at once. */
int ws_probe(const char *url, const char *ifname, int timeout_ms);

/* Next frame header. Returns 0, -EAGAIN when nothing arrived within the socket timeout, or a negative errno.
 */
int ws_read_header(ws_t *ws, uint8_t *op, bool *fin, uint64_t *len);
/* Read exactly `len` payload bytes. */
int ws_read(ws_t *ws, void *buf, size_t len);
int ws_skip(ws_t *ws, uint64_t len);

/* Send one frame. Payloads above 8 KB are not needed by these protocols and are refused. */
int ws_send(ws_t *ws, uint8_t op, const void *data, size_t len);
