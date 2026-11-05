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
    SemaphoreHandle_t tx;
    uint8_t *frame; /* send buffer, guarded by `tx` */
} ws_t;

void ws_init(ws_t *ws);
/*
 * `extra_headers` is a list of "Name: value\r\n" lines or NULL. `ifname` forces the interface (lwIP name),
 * NULL uses the routing table. Returns 0 or a negative errno.
 */
int ws_connect(ws_t *ws, const char *url, const char *subprotocol, const char *extra_headers,
               const char *ifname, int timeout_ms);
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

/* Send one frame. Payloads above 4 KB are not needed by this protocol and are refused. */
int ws_send(ws_t *ws, uint8_t op, const void *data, size_t len);
