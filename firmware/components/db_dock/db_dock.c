#include "db_dock.h"
#include "db_log.h"
#include "db_net.h"
#include "nvs.h"

#include <errno.h>
#include <stdio.h>
#include <string.h>

#include "db_cora.h"
#include "db_dns.h"
#include "db_deck.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_mac.h"
#include "esp_random.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "lwip/dns.h"
#include "lwip/etharp.h"
#include "lwip/priv/tcpip_priv.h"
#include "lwip/sockets.h"
#include "lwip/tcpip.h"
#include "mdns.h"

#define PAYLOAD_MAX  8192
#define KEEPALIVE_MS 2500  /* Companion drops a connection after 5 s without data */
#define ANNOUNCE_MS  30000 /* the Stream Deck app drops a dock it has not heard from for a minute */
#define FEATURE_LEN  64
#define IDLE_TIMEOUT 10 /* s without data from the host */

static const char *TAG = "dock";

/* One host connection. The dock port answers about the dock, the device port passes HID through. */
typedef struct {
    bool device_port;
    uint8_t connection_no;
    volatile int sock;
    uint32_t ip;
    uint32_t local_ip;    /* the bridge's address this host reached */
    uint16_t peer_port;   /* network order */
    db_controller_t kind; /* DB_CTRL_SERVER until the first message tells */
    SemaphoreHandle_t tx;
} conn_t;

static struct {
    void (*hosts_changed)(void);
    conn_t docks[DB_DOCK_HOSTS];
    conn_t devices[DB_DOCK_HOSTS];
    db_cora_dock_t info;
    db_device_info_t attached;
    SemaphoreHandle_t info_lock;
} s;

static void note_kind(uint32_t ip, db_controller_t kind);

static void send_frame(conn_t *c, uint16_t flags, uint8_t op, uint32_t id, const uint8_t *payload, size_t len)
{
    xSemaphoreTake(c->tx, portMAX_DELAY);
    int sock = c->sock;
    if (sock >= 0) {
        uint8_t hdr[DB_CORA_HDR_LEN];
        db_cora_write_hdr(hdr, &(db_cora_hdr_t){.flags = flags, .op = op, .id = id, .len = (uint32_t)len});
        if (send(sock, hdr, sizeof(hdr), 0) < 0 || send(sock, payload, len, 0) < 0) {
            shutdown(sock, SHUT_RDWR);
        }
    }
    xSemaphoreGive(c->tx);
}

static void send_dock_report(conn_t *c, uint8_t cmd, uint32_t id)
{
    uint8_t out[DB_CORA_REPORT_LEN];
    xSemaphoreTake(s.info_lock, portMAX_DELAY);
    db_cora_dock_report(cmd, &s.info, out);
    xSemaphoreGive(s.info_lock);
    send_frame(c, 0, 0, id, out, sizeof(out));
}

_Static_assert(DB_DOCK_HOSTS == DB_SOURCE_DOCKS, "one deck source per dock host");

static db_source_t source_of(const conn_t *c)
{
    return (db_source_t)(DB_SOURCE_DOCK0 + (c - s.devices));
}

static void report_clients(void)
{
    if (s.hosts_changed) {
        s.hosts_changed();
    }
}

static void handle_dock(conn_t *c, const db_cora_hdr_t *h, const uint8_t *p)
{
    /* Companion asks with GET_REPORT, the Stream Deck app with a plain write of the same bytes. Keepalive
     * acknowledgements (03 1a) are plain writes from both. */
    bool query = h->len >= 2 && p[0] == 0x03 && db_cora_is_query(p[1]);
    if (c->kind == DB_CTRL_SERVER && query) {
        c->kind = h->op == DB_CORA_OP_GET_REPORT ? DB_CTRL_COMPANION : DB_CTRL_APP;
        ESP_LOGI(TAG, "dock host is %s", db_controller_name(c->kind));
        note_kind(c->ip, c->kind);
        report_clients(); /* the writer is picked by this kind: without it a host that is ready waited for the
                             next event */
    }
    if (query) {
        send_dock_report(c, p[1], h->id);
    } else if (h->len >= 2 && p[0] == 0x03 && p[1] != 0x1a) {
        ESP_LOGI(TAG, "dock request 03 %02x (op %u, %lu bytes) not handled", p[1], h->op,
                 (unsigned long)h->len);
    }
}

static void handle_device(conn_t *c, const db_cora_hdr_t *h, const uint8_t *p)
{
    if (c->kind == DB_CTRL_SERVER) {
        c->kind = (h->flags & DB_CORA_FLAG_REQ_ACK) ? DB_CTRL_APP : DB_CTRL_COMPANION;
        ESP_LOGI(TAG, "device host is %s", db_controller_name(c->kind));
        note_kind(c->ip, c->kind);
        db_log_mark(c->kind == DB_CTRL_APP ? DB_MARK_APP : DB_MARK_COMPANION);
        report_clients();
    }
    if (!(h->flags & DB_CORA_FLAG_VERBATIM)) {
        /* Hosts ask the device port which device it carries. 0x80 stays unanswered on purpose: an answer
         * would make the host treat this port as a dock. */
        if (h->op == DB_CORA_OP_GET_REPORT && h->len >= 2 && p[0] == 0x03 && p[1] == 0x1c) {
            send_dock_report(c, 0x1c, h->id);
        }
        return;
    }
    switch (h->op) {
    case DB_CORA_OP_WRITE:
        db_deck_raw_write(source_of(c), p, h->len);
        break;
    case DB_CORA_OP_SEND_REPORT:
        db_deck_raw_set_feature(source_of(c), p, h->len);
        break;
    case DB_CORA_OP_GET_REPORT: {
        /* The Stream Deck app sends a whole report and gets the same length back. Companion sends only the
         * report id and needs the full 32 bytes, or the serial number is lost. */
        size_t want = h->len < 2 ? 32 : (h->len < FEATURE_LEN ? h->len : FEATURE_LEN);
        uint8_t buf[FEATURE_LEN] = {h->len ? p[0] : 0};
        size_t n = 0;
        if (db_deck_raw_get_feature(buf, want, &n) != ESP_OK) {
            ESP_LOGW(TAG, "feature report 0x%02x failed", buf[0]);
        }
        send_frame(c, DB_CORA_FLAG_VERBATIM | DB_CORA_FLAG_RESULT, DB_CORA_OP_GET_REPORT, h->id, buf, want);
        return;
    }
    default:
        break;
    }
    /* Only the Stream Deck app asks for acknowledgements; that also tells the hosts apart. */
    if (h->flags & DB_CORA_FLAG_REQ_ACK) {
        static const uint8_t zero[4];
        send_frame(c, DB_CORA_FLAG_VERBATIM | DB_CORA_FLAG_ACK_NAK, 0, h->id, zero, sizeof(zero));
    }
}

static bool read_full(int sock, uint8_t *buf, size_t len)
{
    while (len > 0) {
        int n = recv(sock, buf, len, 0);
        if (n <= 0) {
            return false;
        }
        buf += n;
        len -= (size_t)n;
    }
    return true;
}

static volatile bool rejoining;
static uint32_t rejoin_ip, rejoin_local_ip; /* one rejoin at a time, while rejoining */
static void rejoin_app(void *arg);

static void serve(conn_t *c, int sock, uint8_t *payload)
{
    struct timeval tv = {.tv_sec = IDLE_TIMEOUT};
    setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
    /* A host that stops reading must not block the deck's input path, which sends from its own task. */
    struct timeval send_tv = {.tv_sec = 2};
    setsockopt(sock, SOL_SOCKET, SO_SNDTIMEO, &send_tv, sizeof(send_tv));
    int one = 1;
    setsockopt(sock, IPPROTO_TCP, TCP_NODELAY, &one, sizeof(one));
    c->sock = sock;
    /* Greet with a keepalive at once; the Stream Deck app waits for it before it trusts a port.
     */
    uint8_t hello[DB_CORA_REPORT_LEN];
    db_cora_keepalive(c->connection_no, hello);
    send_frame(c, 0, 0, 0, hello, sizeof(hello));
    uint8_t hdr_buf[DB_CORA_HDR_LEN];
    db_cora_hdr_t h;
    while (read_full(sock, hdr_buf, sizeof(hdr_buf)) &&
           db_cora_parse_hdr(hdr_buf, sizeof(hdr_buf), &h) == DB_OK && h.len <= PAYLOAD_MAX &&
           read_full(sock, payload, h.len)) {
        if (c->device_port) {
            handle_device(c, &h, payload);
        } else {
            handle_dock(c, &h, payload);
        }
    }
    xSemaphoreTake(c->tx, portMAX_DELAY);
    c->sock = -1;
    xSemaphoreGive(c->tx);
    close(sock);
    ESP_LOGI(TAG, "%s host left (%s)", c->device_port ? "device" : "dock", db_controller_name(c->kind));
    if (c->device_port) {
        db_deck_forget(source_of(c)); /* the next host in this slot must not show this one's picture */
    } else if (c->kind == DB_CTRL_APP && !rejoining) {
        rejoining = true;
        rejoin_ip = c->ip;
        rejoin_local_ip = c->local_ip;
        if (xTaskCreate(rejoin_app, "dock_rejoin", 3072, NULL, 3, NULL) != pdPASS) {
            rejoining = false;
        }
    }
    report_clients();
}

static int listen_on(uint16_t port, int backlog)
{
    int ls = socket(AF_INET, SOCK_STREAM, 0);
    int one = 1;
    setsockopt(ls, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(one));
    struct sockaddr_in addr = {.sin_family = AF_INET, .sin_port = htons(port)};
    if (bind(ls, (struct sockaddr *)&addr, sizeof(addr)) != 0 || listen(ls, backlog) != 0) {
        ESP_LOGE(TAG, "cannot listen on %u", port);
        close(ls);
        return -1;
    }
    return ls;
}

static void host_task(void *arg)
{
    conn_t *c = arg;
    uint8_t *payload = heap_caps_malloc(PAYLOAD_MAX, MALLOC_CAP_SPIRAM);
    if (payload) {
        serve(c, c->sock, payload);
    } else {
        ESP_LOGE(TAG, "no memory for a host connection");
        close(c->sock);
        c->sock = -1;
    }
    heap_caps_free(payload);
    vTaskDelete(NULL);
}

static char dock_instance[48];
static void announce(void);

/* The hosts of the last boot, so that after a power cut their old connections can be ended. */
typedef struct {
    uint32_t ip;
    uint32_t local_ip; /* ours: the reset must come from the address the connection used */
    uint16_t port;     /* theirs, network order */
    uint16_t local;    /* ours */
} peer_t;

#define NVS_PEERS "dock_peers"

static void save_peers(void) /* from both listen tasks: under info_lock */
{
    static peer_t saved[2 * DB_DOCK_HOSTS];
    peer_t now[2 * DB_DOCK_HOSTS] = {0};
    for (int i = 0; i < 2 * DB_DOCK_HOSTS; i++) {
        const conn_t *c = i < DB_DOCK_HOSTS ? &s.docks[i] : &s.devices[i - DB_DOCK_HOSTS];
        if (c->sock >= 0) {
            now[i] = (peer_t){c->ip, c->local_ip, c->peer_port,
                              c->device_port ? DB_CORA_DEVICE_PORT : DB_CORA_DOCK_PORT};
        }
    }
    if (memcmp(now, saved, sizeof(now)) == 0) {
        return; /* a write only when a host connected anew */
    }
    memcpy(saved, now, sizeof(now));
    nvs_handle_t h;
    if (nvs_open("dock", NVS_READWRITE, &h) == ESP_OK) {
        nvs_set_blob(h, NVS_PEERS, now, sizeof(now));
        nvs_commit(h);
        nvs_close(h);
    }
}

static bool answers_arp(uint32_t ip, uint32_t local_ip);

/* the Stream Deck app marks the dock offline and waits for the service to come online again; it misses an
 * announcement that comes before it has noticed */
static void announce_anew(void)
{
    if (!dock_instance[0]) {
        return;
    }
    vTaskDelay(pdMS_TO_TICKS(3000));
    mdns_service_remove_for_host(dock_instance, "_elg", "_tcp", NULL);
    vTaskDelay(pdMS_TO_TICKS(500));
    announce();
}

/*
 * After a power cut a host still holds its old connection, which looks alive to it. Companion notices by
 * itself; the Stream Deck app waits for data that never comes and does not connect again for minutes. A SYN
 * from our old port to its old port makes its stack answer with an ACK (RFC 5961), which our stack answers
 * with a reset that fits exactly: the host sees its connection end, as after a clean restart.
 */
static void reset_old_peers(void *arg)
{
    peer_t peers[2 * DB_DOCK_HOSTS] = {0};
    size_t len = sizeof(peers);
    nvs_handle_t h;
    if (nvs_open("dock", NVS_READONLY, &h) != ESP_OK) {
        vTaskDelete(NULL);
        return;
    }
    esp_err_t err = nvs_get_blob(h, NVS_PEERS, peers, &len);
    nvs_close(h);
    if (err != ESP_OK) {
        vTaskDelete(NULL);
        return;
    }
    int socks[2 * DB_DOCK_HOSTS];
    int n = 0;
    /* lwIP binds to an address no interface has yet, and the reset would leave the wrong way: wait until the
     * old address is up again, LAN can take 10 s */
    for (int round = 0; round < 100; round++) {
        bool waiting = false;
        db_net_status_t net;
        db_net_status(&net);
        for (int i = 0; i < 2 * DB_DOCK_HOSTS; i++) {
            if (!peers[i].ip) {
                continue;
            }
            bool up = (net.lan_up && inet_addr(net.lan_ip) == peers[i].local_ip) ||
                      (net.wlan_up && inet_addr(net.wlan_ip) == peers[i].local_ip);
            /* an address can be up before the switch port forwards (a fixed or restored one), and a SYN sent
             * then is lost: the host's ARP answer shows the path works */
            if (!up || !answers_arp(peers[i].ip, peers[i].local_ip)) {
                waiting = true;
                continue;
            }
            int sock = socket(AF_INET, SOCK_STREAM, 0);
            if (sock < 0) {
                continue;
            }
            int one = 1;
            setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(one));
            fcntl(sock, F_SETFL, O_NONBLOCK);
            struct sockaddr_in local = {.sin_family = AF_INET,
                                        .sin_port = htons(peers[i].local),
                                        .sin_addr.s_addr = peers[i].local_ip};
            struct sockaddr_in peer = {
                .sin_family = AF_INET, .sin_port = peers[i].port, .sin_addr.s_addr = peers[i].ip};
            if (bind(sock, (struct sockaddr *)&local, sizeof(local)) == 0) {
                connect(sock, (struct sockaddr *)&peer, sizeof(peer));
                socks[n++] = sock;
                peers[i].ip = 0;
            } else {
                close(sock);
                waiting = true;
            }
        }
        if (!waiting) {
            break;
        }
        vTaskDelay(pdMS_TO_TICKS(200));
    }
    vTaskDelay(pdMS_TO_TICKS(500));
    for (int i = 0; i < n; i++) {
        close(socks[i]);
    }
    ESP_LOGI(TAG, "ended %d connections of the last boot", n);
    if (n) {
        announce_anew();
    }
    vTaskDelete(NULL);
}

/* lwIP cannot drop a single entry; the other hosts are asked again on their next packet */
static err_t forget_arp(struct tcpip_api_call_data *call)
{
    struct netif *n;
    NETIF_FOREACH(n)
    {
        if (n->flags & NETIF_FLAG_ETHARP) {
            etharp_cleanup_netif(n);
        }
    }
    return ERR_OK;
}

/* The Stream Deck app marks a dock offline when its connection dies, also when only the path in between was
 * down and the link stayed up, and comes back only for a new announcement. A fresh ARP answer shows the path
 * works again. */
static void rejoin_app(void *arg)
{
    struct tcpip_api_call_data call;
    tcpip_api_call(forget_arp, &call);
    for (int i = 0; i < 600; i++) {
        if (answers_arp(rejoin_ip, rejoin_local_ip)) {
            announce_anew();
            break;
        }
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
    rejoining = false;
    vTaskDelete(NULL);
}

/*
 * The hosts that connected, by MAC address, so a laptop that gets a new address from DHCP stays the same
 * host. With dock_paired_only off (the default) everyone may connect and the list is an overview. With it on,
 * a new host gets in and is remembered while none is known, in the first minutes after a boot, or while
 * pairing is open; otherwise it is turned away and waits as a request that the deck or the web UI answers.
 */
#define KNOWN_MAX  8
#define OPEN_US    (5 * 60 * 1000000LL)
#define REQUEST_US (60 * 1000000LL)

static struct {
    portMUX_TYPE mux;
    db_dock_peer_t known[KNOWN_MAX];
    int count;
    int64_t open_until;
    uint32_t request;
    uint8_t request_mac[6];
    int64_t request_at;
    struct {
        uint32_t ip;
        char name[DB_DNS_NAME_LEN];
        int64_t asked_at;
    } names[KNOWN_MAX]; /* RAM only: a name is asked again after a boot, it may have changed */
    int next_name;
} pair = {.mux = portMUX_INITIALIZER_UNLOCKED, .open_until = OPEN_US};

static void lookup_task(void *arg)
{
    uint32_t ip = (uint32_t)(uintptr_t)arg;
    char name[DB_DNS_NAME_LEN] = "";
    const ip_addr_t *dns = dns_getserver(0);
    int sock = socket(AF_INET, SOCK_DGRAM, 0);
    if (sock >= 0 && dns && !ip_addr_isany(dns)) {
        struct timeval wait = {.tv_sec = 1};
        setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, &wait, sizeof(wait));
        struct sockaddr_in to = {.sin_family = AF_INET, .sin_port = htons(53)};
        to.sin_addr.s_addr = ip_2_ip4(dns)->addr;
        uint16_t id = (uint16_t)esp_random();
        uint8_t query[64], answer[512];
        size_t n = db_dns_ptr_query(ip, id, query);
        for (int i = 0; i < 2 && !name[0]; i++) {
            sendto(sock, query, n, 0, (struct sockaddr *)&to, sizeof(to));
            int got = recv(sock, answer, sizeof(answer), 0);
            if (got > 0) {
                db_dns_ptr_name(answer, (size_t)got, id, name);
            }
        }
    }
    if (sock >= 0) {
        close(sock);
    }
    taskENTER_CRITICAL(&pair.mux);
    for (int i = 0; i < KNOWN_MAX; i++) {
        if (pair.names[i].ip == ip) {
            snprintf(pair.names[i].name, sizeof(pair.names[i].name), "%s", name);
        }
    }
    taskEXIT_CRITICAL(&pair.mux);
    if (name[0] && s.hosts_changed) {
        s.hosts_changed();
    }
    vTaskDelete(NULL);
}

/* Under the mux. Without an answer it asks again after a minute, not on every call. */
static bool should_ask(uint32_t ip, int64_t now)
{
    for (int i = 0; i < KNOWN_MAX; i++) {
        if (pair.names[i].ip == ip) {
            if (pair.names[i].name[0] || now - pair.names[i].asked_at < 60000000) {
                return false;
            }
            pair.names[i].asked_at = now;
            return true;
        }
    }
    int i = pair.next_name++ % KNOWN_MAX;
    pair.names[i].ip = ip;
    pair.names[i].name[0] = 0;
    pair.names[i].asked_at = now;
    return true;
}

static void look_up(uint32_t ip)
{
    int64_t now = esp_timer_get_time();
    taskENTER_CRITICAL(&pair.mux);
    bool ask = should_ask(ip, now);
    taskEXIT_CRITICAL(&pair.mux);
    if (ask) {
        xTaskCreate(lookup_task, "dock_name", 3072, (void *)(uintptr_t)ip, 2, NULL);
    }
}

void db_dock_name(uint32_t ip, char name[DB_DNS_NAME_LEN])
{
    name[0] = 0;
    taskENTER_CRITICAL(&pair.mux);
    for (int i = 0; i < KNOWN_MAX; i++) {
        if (pair.names[i].ip == ip) {
            snprintf(name, DB_DNS_NAME_LEN, "%s", pair.names[i].name);
        }
    }
    taskEXIT_CRITICAL(&pair.mux);
}

static const uint8_t no_mac[6];

typedef struct {
    struct tcpip_api_call_data call;
    ip4_addr_t ip;
    uint32_t local_ip;
    uint8_t mac[6];
} arp_query_t;

static err_t arp_lookup(struct tcpip_api_call_data *call)
{
    arp_query_t *q = (arp_query_t *)call;
    struct netif *n;
    NETIF_FOREACH(n)
    {
        struct eth_addr *eth;
        const ip4_addr_t *found;
        if (etharp_find_addr(n, &q->ip, &eth, &found) >= 0) {
            memcpy(q->mac, eth->addr, 6);
            return ERR_OK;
        }
    }
    return ERR_VAL;
}

/* A host that just connected is in the ARP table; one behind a router is not, and is known by address only.
 */
static void mac_of(uint32_t ip, uint8_t mac[6])
{
    arp_query_t q = {.ip.addr = ip};
    tcpip_api_call(arp_lookup, &q.call);
    memcpy(mac, q.mac, 6);
}

static err_t arp_probe(struct tcpip_api_call_data *call)
{
    arp_query_t *q = (arp_query_t *)call;
    struct netif *n;
    NETIF_FOREACH(n)
    {
        if (!(n->flags & NETIF_FLAG_ETHARP) || !netif_is_up(n) || !netif_is_link_up(n) ||
            netif_ip4_addr(n)->addr != q->local_ip) {
            continue;
        }
        const ip4_addr_t *hop =
            ip4_addr_net_eq(&q->ip, netif_ip4_addr(n), netif_ip4_netmask(n)) ? &q->ip : netif_ip4_gw(n);
        struct eth_addr *eth;
        const ip4_addr_t *found;
        if (etharp_find_addr(n, hop, &eth, &found) >= 0) {
            return ERR_OK;
        }
        etharp_request(n, hop);
    }
    return ERR_VAL;
}

/* asked on the interface with our address local_ip: WLAN is in the same subnet and answers when LAN does not */
static bool answers_arp(uint32_t ip, uint32_t local_ip)
{
    arp_query_t q = {.ip.addr = ip, .local_ip = local_ip};
    return tcpip_api_call(arp_probe, &q.call) == ERR_OK;
}

static void save_known(void)
{
    db_dock_peer_t copy[KNOWN_MAX];
    taskENTER_CRITICAL(&pair.mux);
    memcpy(copy, pair.known, sizeof(copy));
    int n = pair.count;
    taskEXIT_CRITICAL(&pair.mux);
    nvs_handle_t h;
    if (nvs_open("dock", NVS_READWRITE, &h) == ESP_OK) {
        nvs_set_blob(h, "hosts", copy, (size_t)n * sizeof(copy[0]));
        nvs_commit(h);
        nvs_close(h);
    }
}

static void load_known(void)
{
    size_t len = sizeof(pair.known);
    nvs_handle_t h;
    if (nvs_open("dock", NVS_READONLY, &h) == ESP_OK) {
        if (nvs_get_blob(h, "hosts", pair.known, &len) == ESP_OK) {
            pair.count = (int)(len / sizeof(pair.known[0]));
        }
        nvs_close(h);
    }
}

/* The entry of this host: by MAC when both have one, else by address. -1 when unknown. Under the mux. */
static int find(uint32_t ip, const uint8_t mac[6])
{
    for (int i = 0; i < pair.count; i++) {
        bool macs = memcmp(mac, no_mac, 6) && memcmp(pair.known[i].mac, no_mac, 6);
        if (macs ? memcmp(pair.known[i].mac, mac, 6) == 0 : pair.known[i].ip == ip) {
            return i;
        }
    }
    return -1;
}

/* Under the mux. When full, an open dock drops its oldest entry; a strict one takes nobody new, so an address
 * cannot push out a paired host. */
static bool remember(uint32_t ip, const uint8_t mac[6], bool strict)
{
    if (pair.count == KNOWN_MAX && strict) {
        return false;
    }
    if (pair.count == KNOWN_MAX) {
        memmove(pair.known, pair.known + 1, sizeof(pair.known) - sizeof(pair.known[0]));
        pair.count--;
    }
    pair.known[pair.count++] = (db_dock_peer_t){.ip = ip};
    memcpy(pair.known[pair.count - 1].mac, mac, 6);
    return true;
}

static bool admit(uint32_t ip)
{
    look_up(ip);
    uint8_t mac[6];
    mac_of(ip, mac);
    bool strict = db_config()->dock_paired_only;
    int64_t now = esp_timer_get_time();
    bool in = false;
    bool changed = false;
    taskENTER_CRITICAL(&pair.mux);
    int i = find(ip, mac);
    if (i >= 0) {
        in = true;
        changed = pair.known[i].ip != ip || (memcmp(mac, no_mac, 6) && memcmp(pair.known[i].mac, mac, 6));
        pair.known[i].ip = ip; /* a new address from DHCP */
        if (memcmp(mac, no_mac, 6)) {
            memcpy(pair.known[i].mac, mac, 6);
        }
    } else if (!strict || pair.count == 0 || now < pair.open_until) {
        in = changed = remember(ip, mac, strict);
    }
    bool asking = pair.request && now - pair.request_at < REQUEST_US;
    if (!in && (!asking || pair.request == ip)) {
        /* one request at a time: another address must not take the place of the one on the deck */
        pair.request = ip;
        memcpy(pair.request_mac, mac, 6);
        pair.request_at = now;
    }
    taskEXIT_CRITICAL(&pair.mux);
    if (changed) {
        save_known();
    }
    if (!in && s.hosts_changed) {
        s.hosts_changed(); /* the deck shows the request */
    }
    return in;
}

/* What a host told about itself, for the overview. */
static void note_kind(uint32_t ip, db_controller_t kind)
{
    bool changed = false;
    taskENTER_CRITICAL(&pair.mux);
    for (int i = 0; i < pair.count; i++) {
        if (pair.known[i].ip == ip && pair.known[i].kind != kind) {
            pair.known[i].kind = (uint8_t)kind;
            changed = true;
        }
    }
    taskEXIT_CRITICAL(&pair.mux);
    if (changed) {
        save_known();
    }
}

bool db_dock_request(char ip[16])
{
    taskENTER_CRITICAL(&pair.mux);
    uint32_t r = esp_timer_get_time() - pair.request_at < REQUEST_US ? pair.request : 0;
    taskEXIT_CRITICAL(&pair.mux);
    if (r) {
        inet_ntoa_r(*(struct in_addr *)&r, ip, 16);
    }
    return r != 0;
}

void db_dock_answer(bool accept)
{
    taskENTER_CRITICAL(&pair.mux);
    uint32_t r = pair.request;
    pair.request = 0;
    bool added = accept && r && find(r, pair.request_mac) < 0 && remember(r, pair.request_mac, false);
    taskEXIT_CRITICAL(&pair.mux);
    if (added) {
        save_known();
    }
    if (s.hosts_changed) {
        s.hosts_changed();
    }
}

void db_dock_pairing(int seconds)
{
    taskENTER_CRITICAL(&pair.mux);
    pair.open_until = esp_timer_get_time() + (int64_t)seconds * 1000000;
    taskEXIT_CRITICAL(&pair.mux);
}

int db_dock_pairing_left(void)
{
    int64_t left = pair.open_until - esp_timer_get_time();
    return left > 0 ? (int)(left / 1000000) : 0;
}

size_t db_dock_known(db_dock_peer_t *out, size_t max)
{
    taskENTER_CRITICAL(&pair.mux);
    size_t n = (size_t)pair.count < max ? (size_t)pair.count : max;
    memcpy(out, pair.known, n * sizeof(out[0]));
    taskEXIT_CRITICAL(&pair.mux);
    for (size_t i = 0; i < n; i++) {
        look_up(out[i].ip);
    }
    return n;
}

bool db_dock_peer_connected(const db_dock_peer_t *p)
{
    for (int i = 0; i < 2 * DB_DOCK_HOSTS; i++) {
        const conn_t *c = i < DB_DOCK_HOSTS ? &s.docks[i] : &s.devices[i - DB_DOCK_HOSTS];
        if (c->sock >= 0 && c->ip == p->ip) {
            return true;
        }
    }
    return false;
}

void db_dock_forget(const char *ip)
{
    uint32_t addr = ip ? inet_addr(ip) : 0;
    taskENTER_CRITICAL(&pair.mux);
    int kept = 0;
    for (int i = 0; i < pair.count; i++) {
        if (ip && pair.known[i].ip != addr) {
            pair.known[kept++] = pair.known[i];
        }
    }
    pair.count = kept;
    taskEXIT_CRITICAL(&pair.mux);
    save_known();
}

static void listen_task(void *arg)
{
    bool device_port = arg != NULL;
    conn_t *pool = device_port ? s.devices : s.docks;
    uint16_t port = device_port ? DB_CORA_DEVICE_PORT : DB_CORA_DOCK_PORT;
    int ls = -1;
    for (;;) {
        if (ls < 0) {
            ls = listen_on(port, DB_DOCK_HOSTS);
        }
        struct sockaddr_in from;
        socklen_t len = sizeof(from);
        int sock = ls >= 0 ? accept(ls, (struct sockaddr *)&from, &len) : -1;
        if (sock < 0) {
            if (ls >= 0) {
                ESP_LOGW(TAG, "port %u: accept failed: %s", port, strerror(errno));
            }
            vTaskDelay(pdMS_TO_TICKS(1000));
            continue;
        }
        conn_t *c = NULL;
        for (int i = 0; i < DB_DOCK_HOSTS && !c; i++) {
            c = pool[i].sock < 0 ? &pool[i] : NULL;
        }
        char ip[16];
        inet_ntoa_r(from.sin_addr, ip, sizeof(ip));
        if (!admit(from.sin_addr.s_addr)) {
            ESP_LOGW(TAG, "port %u: %s is not paired, asking on the deck", port, ip);
            close(sock);
            continue;
        }
        if (!c) {
            ESP_LOGW(TAG, "port %u: %s refused, %d hosts connected", port, ip, DB_DOCK_HOSTS);
            close(sock);
            continue;
        }
        ESP_LOGI(TAG, "%s host %s", device_port ? "device" : "dock", ip);
        c->ip = from.sin_addr.s_addr;
        c->peer_port = from.sin_port;
        struct sockaddr_in at;
        socklen_t at_len = sizeof(at);
        c->local_ip = getsockname(sock, (struct sockaddr *)&at, &at_len) == 0 ? at.sin_addr.s_addr : 0;
        c->kind = DB_CTRL_SERVER;
        c->sock = sock;
        xSemaphoreTake(s.info_lock, portMAX_DELAY);
        save_peers();
        xSemaphoreGive(s.info_lock);
        if (xTaskCreatePinnedToCore(host_task, device_port ? "dock_dev" : "dock_host", 4096, c,
                                    device_port ? 7 : 6, NULL, 0) != pdPASS) {
            ESP_LOGE(TAG, "no memory for a host task, %s refused", ip);
            close(sock);
            c->sock = -1;
        }
    }
}

static char device_instance[32];

static void reannounce(void)
{
    if (!dock_instance[0]) {
        announce();
        return;
    }
    /* setting the unchanged port is the API's only way to send the full service announcement again */
    mdns_service_port_set_for_host(dock_instance, "_elg", "_tcp", NULL, DB_CORA_DOCK_PORT);
    xSemaphoreTake(s.info_lock, portMAX_DELAY);
    if (device_instance[0]) {
        mdns_service_port_set_for_host(device_instance, "_elg", "_tcp", NULL, DB_CORA_DEVICE_PORT);
    }
    xSemaphoreGive(s.info_lock);
}

static void keepalive_task(void *arg)
{
    uint8_t buf[DB_CORA_REPORT_LEN];
    for (int tick = 1;; tick++) {
        vTaskDelay(pdMS_TO_TICKS(KEEPALIVE_MS));
        if (tick % (ANNOUNCE_MS / KEEPALIVE_MS) == 0) {
            reannounce();
        }
        for (int i = 0; i < 2 * DB_DOCK_HOSTS; i++) {
            conn_t *c = i < DB_DOCK_HOSTS ? &s.docks[i] : &s.devices[i - DB_DOCK_HOSTS];
            if (c->sock >= 0) {
                db_cora_keepalive(c->connection_no, buf);
                send_frame(c, 0, 0, 0, buf, sizeof(buf));
            }
        }
    }
}

/* Input goes to the host that draws, so presses in the menu stay in the menu. */
static void on_raw_input(const uint8_t *report, size_t len)
{
    uint8_t buf[DB_CORA_REPORT_LEN] = {0}; /* the dock forwards full 512 byte input reports */
    memcpy(buf, report, len < sizeof(buf) ? len : sizeof(buf));
    db_source_t writer = db_deck_writer();
    for (int i = 0; i < DB_DOCK_HOSTS; i++) {
        if (s.devices[i].sock >= 0 && s.devices[i].kind != DB_CTRL_SERVER &&
            source_of(&s.devices[i]) == writer) {
            send_frame(&s.devices[i], DB_CORA_FLAG_VERBATIM, 0, 0, buf, sizeof(buf));
        }
    }
}

/* The attached device is also announced as its own service on the device port. */
static void device_service(const db_device_info_t *info)
{
    if (device_instance[0]) {
        mdns_service_remove_for_host(device_instance, "_elg", "_tcp", NULL);
        device_instance[0] = '\0';
    }
    if (!info) {
        return;
    }
    char vid[8];
    char pid[8];
    snprintf(vid, sizeof(vid), "0x%04X", info->vid);
    snprintf(pid, sizeof(pid), "0x%04X", info->pid);
    mdns_txt_item_t txt[] = {
        {"dt", "213"}, {"mf", "Elgato"}, {"md", info->model},    {"pv", "1.0"}, {"sn", info->serial},
        {"vid", vid},  {"pid", pid},     {"fw", info->firmware}, {"prt", "2"},
    };
    strlcpy(device_instance, info->serial, sizeof(device_instance));
    esp_err_t err = mdns_service_add_for_host(device_instance, "_elg", "_tcp", NULL, DB_CORA_DEVICE_PORT, txt,
                                              sizeof(txt) / sizeof(txt[0]));
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "mDNS device announcement failed: %s", esp_err_to_name(err));
        device_instance[0] = '\0';
    }
}

/* Unasked 0x1c reports tell connected hosts that a device came or went; they then open or close the device
 * port. The Stream Deck app drops a dock that had no device for it and only comes back when the service goes
 * offline and online again, so the service is announced anew as well. */
static void tell_hosts(void)
{
    for (int i = 0; i < DB_DOCK_HOSTS; i++) {
        if (s.docks[i].sock >= 0 && s.docks[i].kind != DB_CTRL_SERVER) {
            send_dock_report(&s.docks[i], 0x1c, 0);
        }
    }
}

static esp_timer_handle_t services_timer;

static void sync_services(void *arg)
{
    xSemaphoreTake(s.info_lock, portMAX_DELAY);
    device_service(s.info.device);
    xSemaphoreGive(s.info_lock);
    if (dock_instance[0]) {
        mdns_service_remove_for_host(dock_instance, "_elg", "_tcp", NULL);
        announce();
    }
}

/* The services wait until the deck was there or gone for a second: a deck that comes and goes fast made them go
 * offline and online several times, and the Stream Deck app then never resolved the device again. */
static void on_device(const db_device_info_t *info)
{
    xSemaphoreTake(s.info_lock, portMAX_DELAY);
    if (info) {
        s.attached = *info;
    }
    s.info.device = info ? &s.attached : NULL;
    xSemaphoreGive(s.info_lock);
    tell_hosts();
    esp_timer_stop(services_timer);
    esp_timer_start_once(services_timer, 1000000);
}

size_t db_dock_hosts(db_dock_host_t *out, size_t max)
{
    size_t n = 0;
    for (int i = 0; i < DB_DOCK_HOSTS && n < max; i++) {
        const conn_t *c = &s.docks[i];
        if (c->sock >= 0) {
            int source = -1;
            for (int k = 0; k < DB_DOCK_HOSTS; k++) {
                source =
                    s.devices[k].sock >= 0 && s.devices[k].ip == c->ip ? source_of(&s.devices[k]) : source;
            }
            out[n] = (db_dock_host_t){.kind = c->kind, .source = source};
            struct in_addr a = {.s_addr = c->ip};
            inet_ntoa_r(a, out[n].ip, sizeof(out[n].ip));
            n++;
        }
    }
    return n;
}

static void announce(void)
{
    snprintf(dock_instance, sizeof(dock_instance), "Stream Deck Network Dock %02X%02X%02X", s.info.mac[3],
             s.info.mac[4], s.info.mac[5]);
    char id[18];
    snprintf(id, sizeof(id), "%02X:%02X:%02X:%02X:%02X:%02X", s.info.mac[0], s.info.mac[1], s.info.mac[2],
             s.info.mac[3], s.info.mac[4], s.info.mac[5]);
    mdns_txt_item_t txt[] = {
        {"dt", "215"},
        {"mf", "Elgato"},
        {"md", "Stream Deck Network Dock 20GBW9901"},
        {"pv", "1.0"},
        {"id", id},
        {"sn", s.info.serial},
        {"fw", DB_CORA_FIRMWARE},
        {"hwrev", "0200"},
    };
    esp_err_t err =
        mdns_service_add(dock_instance, "_elg", "_tcp", DB_CORA_DOCK_PORT, txt, sizeof(txt) / sizeof(txt[0]));
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "mDNS announcement failed: %s", esp_err_to_name(err));
        dock_instance[0] = '\0'; /* the next re-announce tries again */
    }
}

/* Runs in esp_restart(). Hosts must see the dock go: the connections end with a reset, and mDNS says goodbye,
 * or the Stream Deck app keeps its cached dock and does not come back after a short restart. */
static void close_hosts(void)
{
    mdns_service_remove_all();
    for (int i = 0; i < 2 * DB_DOCK_HOSTS; i++) {
        conn_t *c = i < DB_DOCK_HOSTS ? &s.docks[i] : &s.devices[i - DB_DOCK_HOSTS];
        int sock = c->sock;
        if (sock >= 0) {
            struct linger now = {.l_onoff = 1, .l_linger = 0};
            setsockopt(sock, SOL_SOCKET, SO_LINGER, &now, sizeof(now));
            close(sock);
        }
    }
    vTaskDelay(pdMS_TO_TICKS(150)); /* the resets and the goodbye leave through other tasks */
}

esp_err_t db_dock_start(void (*hosts_changed)(void))
{
    s.hosts_changed = hosts_changed;
    load_known();
    xTaskCreate(reset_old_peers, "dock_reset", 3072, NULL, 3, NULL);
    esp_register_shutdown_handler(close_hosts);
    s.info_lock = xSemaphoreCreateMutex();
    esp_read_mac(s.info.mac, ESP_MAC_BASE);
    snprintf(s.info.serial, sizeof(s.info.serial), "DB%02X%02X%02X%02X%02X%02X", s.info.mac[0], s.info.mac[1],
             s.info.mac[2], s.info.mac[3], s.info.mac[4], s.info.mac[5]);
    for (int i = 0; i < DB_DOCK_HOSTS; i++) {
        s.docks[i] = (conn_t){.connection_no = 1, .sock = -1, .tx = xSemaphoreCreateMutex()};
        s.devices[i] =
            (conn_t){.device_port = true, .connection_no = 2, .sock = -1, .tx = xSemaphoreCreateMutex()};
    }
    esp_timer_create_args_t timer = {.callback = sync_services, .name = "dock_mdns"};
    esp_timer_create(&timer, &services_timer);
    db_device_info_t now;
    if (db_deck_info(&now)) {
        s.attached = now;
        s.info.device = &s.attached;
        device_service(&now);
    }
    db_deck_listener_t l = {.device_changed = on_device, .raw_input = on_raw_input};
    db_deck_add_listener(&l);
    xTaskCreatePinnedToCore(listen_task, "dock_port", 3072, NULL, 6, NULL, 0);
    xTaskCreatePinnedToCore(listen_task, "dock_dev_port", 3072, (void *)1, 7, NULL, 0);
    xTaskCreatePinnedToCore(keepalive_task, "dock_ka", 3072, NULL, 5, NULL, 0);
    announce();
    ESP_LOGI(TAG, "Network Dock emulation on ports %d and %d", DB_CORA_DOCK_PORT, DB_CORA_DEVICE_PORT);
    return ESP_OK;
}
