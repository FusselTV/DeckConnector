/* lwIP as the firmware uses it, on the host's sockets: no netifs, no ARP table, no DNS server list. */
#pragma once

#include <arpa/inet.h>
#include <fcntl.h>
#include <netdb.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <stdio.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <unistd.h>

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
