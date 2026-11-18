/* lwIP as the firmware uses it, on the host's sockets: no netifs, no ARP table, no DNS server list. */
#pragma once

#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <netdb.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <poll.h>
#include <stdio.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <unistd.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#define LWIP_SOCKET_OFFSET 0
#define ERR_OK             0
#define ERR_VAL            (-6)
#define NETIF_FLAG_ETHARP  0x08U
#define NETIF_FOREACH(n)   for ((n) = NULL; (n); (n) = NULL)

typedef signed char err_t;
typedef struct {
    uint32_t addr;
} ip4_addr_t;
typedef ip4_addr_t ip_addr_t;
struct eth_addr {
    uint8_t addr[6];
};
struct netif {
    uint8_t flags;
};
struct tcpip_api_call_data {
    int unused;
};

#define ip4_addr_get_u32(a) ((a)->addr)
#define ip_2_ip4(a)         (a)
#define ip_addr_isany(a)    (!(a) || !(a)->addr)

static inline int ip4addr_aton(const char *cp, ip4_addr_t *addr)
{
    struct in_addr a;
    int ok = inet_aton(cp, &a);
    addr->addr = ok ? a.s_addr : 0;
    return ok;
}

static inline char *ip4addr_ntoa_r(const ip4_addr_t *addr, char *buf, int len)
{
    struct in_addr a = {addr->addr};
    return inet_ntop(AF_INET, &a, buf, (socklen_t)len) ? buf : NULL;
}

static inline err_t tcpip_api_call(err_t (*fn)(struct tcpip_api_call_data *), struct tcpip_api_call_data *call)
{
    return fn(call);
}

static inline void etharp_cleanup_netif(struct netif *n)
{
}

static inline int etharp_find_addr(struct netif *n, const ip4_addr_t *ip, struct eth_addr **eth,
                                   const ip4_addr_t **found)
{
    return -1;
}

static inline const ip_addr_t *dns_getserver(uint8_t i)
{
    return NULL;
}

#define ipaddr_addr(cp) inet_addr(cp)

static inline char *inet_ntoa_r(struct in_addr a, char *buf, int len)
{
    return inet_ntop(AF_INET, &a, buf, (socklen_t)len) ? buf : NULL;
}

/*
 * The FreeRTOS Linux port runs one task at a time, and a task blocked in the kernel still counts as running: every
 * task of lower priority would wait with it. So a blocking socket call waits as lwIP lets a task wait on the chip:
 * it polls, and sleeps a tick in between. Socket timeouts hold as on the chip.
 */
static inline int sim_wait(int s, short events, int ms)
{
    TickType_t end = xTaskGetTickCount() + pdMS_TO_TICKS(ms < 0 ? 0 : ms);
    for (;;) {
        struct pollfd p = {.fd = s, .events = events};
        int n = poll(&p, 1, 0);
        if (n != 0 && !(n < 0 && errno == EINTR)) {
            return n;
        }
        if (ms >= 0 && (int32_t)(xTaskGetTickCount() - end) >= 0) {
            return 0;
        }
        vTaskDelay(1);
    }
}

/* SO_RCVTIMEO or SO_SNDTIMEO in ms, -1: none. 0 when the socket does not block. */
static inline int sim_timeout(int s, int opt, int flags)
{
    if ((flags & MSG_DONTWAIT) || (fcntl(s, F_GETFL) & O_NONBLOCK)) {
        return 0;
    }
    struct timeval tv = {0};
    socklen_t l = sizeof(tv);
    getsockopt(s, SOL_SOCKET, opt, &tv, &l);
    return tv.tv_sec || tv.tv_usec ? (int)(tv.tv_sec * 1000 + tv.tv_usec / 1000) : -1;
}

static inline int sim_select(int n, fd_set *r, fd_set *w, fd_set *e, struct timeval *tv)
{
    fd_set r0, w0, e0;
    r0 = r ? *r : (fd_set){0};
    w0 = w ? *w : (fd_set){0};
    e0 = e ? *e : (fd_set){0};
    TickType_t end = xTaskGetTickCount() + (tv ? pdMS_TO_TICKS(tv->tv_sec * 1000 + tv->tv_usec / 1000) : 0);
    for (;;) {
        struct timeval now = {0};
        int got = select(n, r, w, e, &now);
        if (got != 0 && !(got < 0 && errno == EINTR)) {
            return got;
        }
        if (tv && (int32_t)(xTaskGetTickCount() - end) >= 0) {
            return 0;
        }
        vTaskDelay(1);
        if (r) {
            *r = r0;
        }
        if (w) {
            *w = w0;
        }
        if (e) {
            *e = e0;
        }
    }
}

static inline ssize_t sim_recvfrom(int s, void *b, size_t n, int f, struct sockaddr *a, socklen_t *l)
{
    int ms = sim_timeout(s, SO_RCVTIMEO, f);
    if (ms && sim_wait(s, POLLIN, ms) == 0) {
        errno = EAGAIN;
        return -1;
    }
    return recvfrom(s, b, n, f | MSG_DONTWAIT, a, l);
}

static inline ssize_t sim_recv(int s, void *b, size_t n, int f)
{
    return sim_recvfrom(s, b, n, f, NULL, NULL);
}

static inline ssize_t sim_send(int s, const void *b, size_t n, int f)
{
    int ms = sim_timeout(s, SO_SNDTIMEO, f);
    if (!ms) {
        return send(s, b, n, f | MSG_NOSIGNAL);
    }
    size_t done = 0;
    while (done < n) {
        if (sim_wait(s, POLLOUT, ms) == 0) {
            break;
        }
        ssize_t got = send(s, (const char *)b + done, n - done, f | MSG_DONTWAIT | MSG_NOSIGNAL);
        if (got < 0 && errno != EAGAIN && errno != EINTR) {
            return done ? (ssize_t)done : -1;
        }
        done += got > 0 ? (size_t)got : 0;
    }
    if (!done) {
        errno = EAGAIN;
        return -1;
    }
    return (ssize_t)done;
}

static inline int sim_accept(int s, struct sockaddr *a, socklen_t *l)
{
    int ms = sim_timeout(s, SO_RCVTIMEO, 0);
    if (ms && sim_wait(s, POLLIN, ms) == 0) {
        errno = EAGAIN;
        return -1;
    }
    return accept(s, a, l);
}

#define select   sim_select
#define recv     sim_recv
#define send     sim_send
#define recvfrom sim_recvfrom
#define accept   sim_accept
