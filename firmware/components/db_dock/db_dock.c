#include "db_dock.h"
#include "db_log.h"
#include "db_net.h"
#include "nvs.h"

#include <errno.h>
#include <stdio.h>
#include <string.h>

#include "db_cora.h"
#include "db_deck.h"
#include "db_dns.h"
#include "db_quirks.h"
#include "db_sat.h"
#include "db_satellite.h"
#include "esp_attr.h"
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
#define ANNOUNCE_MS  30000 /* the Elgato App drops a dock it has not heard from for a minute */
#define IDLE_TIMEOUT 6     /* s without data from the host: two missed answers to the keepalive */

static const char *TAG = "dock";

/* One host connection. The dock port answers about the dock, the device port passes HID through. */
typedef struct {
    int deck;
    int host; /* its deck source is DB_SOURCE_DOCK0 + host */
    bool device_port;
    uint8_t connection_no;
    volatile int sock;
    uint32_t ip;
    uint32_t local_ip;    /* the bridge's address this host reached */
    uint16_t peer_port;   /* network order */
    db_controller_t kind; /* DB_CTRL_SERVER until the first message tells */
    SemaphoreHandle_t tx;
    uint8_t out[DB_CORA_HDR_LEN + DB_CORA_REPORT_LEN]; /* under tx */
} conn_t;

#define CONNS (DB_MAX_DECKS * 2 * DB_DOCK_HOSTS)

static struct {
    void (*hosts_changed)(void);
    db_cora_dock_t info[DB_MAX_DECKS];
    SemaphoreHandle_t info_lock;
} s;

/* By deck, then dock and device port, then host. Zero until db_dock_start: the deck's UI asks for hosts
 * before then. */
static conn_t conns[CONNS] EXT_RAM_BSS_ATTR;
static db_device_info_t attached[DB_MAX_DECKS] EXT_RAM_BSS_ATTR;

static conn_t *conn(int deck, bool device_port, int host)
{
    return &conns[(deck * 2 + device_port) * DB_DOCK_HOSTS + host];
}

static uint16_t port_of(int deck, bool device_port)
{
    return (uint16_t)((device_port ? DB_CORA_DEVICE_PORT : DB_CORA_DOCK_PORT) + 2 * deck);
}

static void note_kind(uint32_t ip, db_controller_t kind);
static bool refuse(conn_t *c, db_controller_t kind);
static void keep_peers(void);
/* the keepalive task saves them: a flash write held a host's first answer 3 to 70 ms */
static bool peers_due;

/* block false: skip a host that is not reading instead of waiting up to the send timeout for it */
static void send_frame_to(conn_t *c, uint16_t flags, uint8_t op, uint32_t id, const uint8_t *payload,
                          size_t len, bool block)
{
    configASSERT(len <= DB_CORA_REPORT_LEN);
    if (xSemaphoreTake(c->tx, block ? portMAX_DELAY : 0) != pdTRUE) {
        return;
    }
    int sock = c->sock;
    if (sock >= 0) {
        /* one send: header and data in one TCP segment */
        db_cora_write_hdr(c->out, &(db_cora_hdr_t){.flags = flags, .op = op, .id = id, .len = (uint32_t)len});
        memcpy(c->out + DB_CORA_HDR_LEN, payload, len);
        size_t want = DB_CORA_HDR_LEN + len;
        ssize_t n = send(sock, c->out, want, block ? 0 : MSG_DONTWAIT);
        if (n > 0 && (size_t)n < want) { /* partly sent: the rest must follow or the stream breaks */
            ssize_t rest = send(sock, c->out + n, want - n, 0);
            n = rest < 0 ? rest : n + rest;
        }
        if (n != (ssize_t)want && !(n < 0 && !block && errno == EWOULDBLOCK)) {
            shutdown(sock, SHUT_RDWR);
        }
    }
    xSemaphoreGive(c->tx);
}

static void send_frame(conn_t *c, uint16_t flags, uint8_t op, uint32_t id, const uint8_t *payload, size_t len)
{
    send_frame_to(c, flags, op, id, payload, len, true);
}

static void send_dock_report(conn_t *c, uint8_t cmd, uint32_t id)
{
    uint8_t out[DB_CORA_REPORT_LEN];
    xSemaphoreTake(s.info_lock, portMAX_DELAY);
    db_cora_dock_report(cmd, &s.info[c->deck], out);
    xSemaphoreGive(s.info_lock);
    send_frame(c, 0, 0, id, out, sizeof(out));
}

_Static_assert(DB_DOCK_HOSTS == DB_SOURCE_DOCKS, "one deck source per dock host");

static db_source_t source_of(const conn_t *c)
{
    return (db_source_t)(DB_SOURCE_DOCK0 + c->host);
}

static void report_clients(void)
{
    if (s.hosts_changed) {
        s.hosts_changed();
    }
}

static void handle_dock(conn_t *c, const db_cora_hdr_t *h, const uint8_t *p)
{
    /* Companion asks with GET_REPORT, the Elgato App with a plain write of the same bytes. Keepalive
     * acknowledgements (03 1a) are plain writes from both. */
    bool query = h->len >= 2 && p[0] == 0x03 && db_cora_is_query(p[1]);
    if (c->kind == DB_CTRL_SERVER && query) {
        if (refuse(c, h->op == DB_CORA_OP_GET_REPORT ? DB_CTRL_COMPANION : DB_CTRL_APP)) {
            return;
        }
        char ip[16];
        inet_ntoa_r(*(struct in_addr *)&c->ip, ip, sizeof(ip));
        db_log_event(TAG, "source", "%s at %s connected", db_controller_name(c->kind), ip);
        note_kind(c->ip, c->kind);
        __atomic_store_n(&peers_due, true, __ATOMIC_RELAXED);
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
        if (refuse(c, (h->flags & DB_CORA_FLAG_REQ_ACK) ? DB_CTRL_APP : DB_CTRL_COMPANION)) {
            return;
        }
        ESP_LOGI(TAG, "device host is %s", db_controller_name(c->kind));
        note_kind(c->ip, c->kind);
        __atomic_store_n(&peers_due, true, __ATOMIC_RELAXED);
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
    const db_device_info_t *dev = s.info[c->deck].device;
    db_quirk_t q = dev ? db_quirk(dev->pid, h->op, p, h->len) : (db_quirk_t){0};
    esp_err_t err = ESP_OK;
    switch (h->op) {
    case DB_CORA_OP_WRITE:
        err = q.block ? ESP_FAIL : db_deck_raw_write(c->deck, source_of(c), p, h->len);
        break;
    case DB_CORA_OP_SEND_REPORT:
        err = q.block ? ESP_FAIL : db_deck_raw_set_feature(c->deck, source_of(c), p, h->len);
        break;
    case DB_CORA_OP_GET_REPORT: {
        /* Always answer a whole 32 byte report with op 0. Companion sends
         * only the report id and needs all 32 bytes, or the serial number is lost. */
        uint8_t buf[32] = {h->len ? p[0] : 0};
        size_t n = 0;
        if (db_deck_raw_get_feature(c->deck, buf, sizeof(buf), &n) != ESP_OK) {
            ESP_LOGW(TAG, "feature report 0x%02x failed", buf[0]);
        }
        send_frame(c, DB_CORA_FLAG_VERBATIM | DB_CORA_FLAG_RESULT, 0, h->id, buf, sizeof(buf));
        return;
    }
    default:
        break;
    }
    if (q.block) {
        ESP_LOGI(TAG, "%02x %02x refused for this deck, as the Elgato dock does", p[0], p[1]);
    } else if (err != ESP_OK) {
        ESP_LOGW(TAG, "%02x %02x (op %u) not passed to the deck: %s", p[0], p[1], h->op,
                 esp_err_to_name(err));
    }
    if (err == ESP_OK && q.wait_ms) {
        vTaskDelay(pdMS_TO_TICKS(q.wait_ms));
    }
    /* Only the Elgato App asks for acknowledgements; that also tells the hosts apart. The dock answers 01
     * when it could not pass the frame on. */
    if (h->flags & DB_CORA_FLAG_REQ_ACK) {
        const uint8_t ack[4] = {err == ESP_OK ? 0 : 1};
        send_frame(c, DB_CORA_FLAG_VERBATIM | DB_CORA_FLAG_ACK_NAK, 0, h->id, ack, sizeof(ack));
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
static int rejoin_deck;
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
    /* Greet with a keepalive at once; the Elgato App waits for it before it trusts a port.
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
    /* everything about this host before the slot is free: the listen task may hand it to the next one */
    db_controller_t kind = c->kind;
    uint32_t ip = c->ip;
    uint32_t local_ip = c->local_ip;
    if (c->device_port) {
        db_deck_forget(c->deck,
                       source_of(c)); /* the next host in this slot must not show this one's picture */
    }
    xSemaphoreTake(c->tx, portMAX_DELAY);
    c->sock = -1;
    c->kind = DB_CTRL_SERVER; /* a reader of this slot must never see a new host with the old one's kind */
    c->ip = 0;
    xSemaphoreGive(c->tx);
    close(sock);
    if (c->device_port && kind != DB_CTRL_SERVER) {
        db_log_event(TAG, "source", "%s left", db_controller_name(kind));
    } else {
        ESP_LOGI(TAG, "%s host left (%s)", c->device_port ? "device" : "dock", db_controller_name(kind));
    }
    if (!c->device_port && kind == DB_CTRL_APP && !rejoining) {
        rejoining = true;
        rejoin_ip = ip;
        rejoin_local_ip = local_ip;
        rejoin_deck = c->deck;
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

static char dock_instance[DB_MAX_DECKS][48]; /* set while the dock service is registered */
/* the keepalive, the deck timer and the rejoin tasks all add and remove the service */
static SemaphoreHandle_t mdns_lock;
static void announce(int deck);
static void withdraw(int deck);

/* The hosts of the last boot, so that after a power cut their old connections can be ended. */
typedef struct {
    uint32_t ip;
    uint32_t local_ip; /* ours: the reset must come from the address the connection used */
    uint16_t port;     /* theirs, network order */
    uint16_t local;    /* ours */
} peer_t;

#define NVS_PEERS "dock_peers"

/* After a host said what it is: a refused one must not write to NVS every few seconds. */
static void keep_peers(void)
{
    static peer_t saved[CONNS];
    static peer_t now[CONNS];
    xSemaphoreTake(s.info_lock, portMAX_DELAY);
    memset(now, 0, sizeof(now));
    for (int i = 0; i < CONNS; i++) {
        const conn_t *c = &conns[i];
        if (c->sock >= 0 && c->kind != DB_CTRL_SERVER) {
            now[i] = (peer_t){c->ip, c->local_ip, c->peer_port, port_of(c->deck, c->device_port)};
        }
    }
    if (memcmp(now, saved, sizeof(now)) == 0) {
        xSemaphoreGive(s.info_lock);
        return; /* a write only when a host connected anew */
    }
    nvs_handle_t h;
    esp_err_t err = nvs_open("dock", NVS_READWRITE, &h);
    if (err == ESP_OK) {
        err = nvs_set_blob(h, NVS_PEERS, now, sizeof(now));
        err = err == ESP_OK ? nvs_commit(h) : err;
        nvs_close(h);
    }
    if (err == ESP_OK) {
        memcpy(saved, now, sizeof(now)); /* only then: a failed write is tried again with the next change */
    } else {
        ESP_LOGW(TAG, "hosts for the power-cut reset not saved: %s", esp_err_to_name(err));
    }
    xSemaphoreGive(s.info_lock);
}

/* the Elgato App marks the dock offline and waits for the service to come online again; it misses an
 * announcement that comes before it has noticed */
static void announce_anew(int deck)
{
    if (!dock_instance[deck][0]) {
        return;
    }
    vTaskDelay(pdMS_TO_TICKS(3000));
    withdraw(deck);
    vTaskDelay(pdMS_TO_TICKS(500));
    announce(deck);
}

/*
 * After a power cut a host still holds its old connection, which looks alive to it. Companion notices by
 * itself; the Elgato App waits for data that never comes and does not connect again for minutes. A SYN
 * from our old port to its old port makes its stack answer with an ACK (RFC 5961), which our stack answers
 * with a reset that fits exactly: the host sees its connection end, as after a clean restart.
 */
static void reset_old_peers(void *arg)
{
    peer_t peers[CONNS] = {0};
    size_t len = sizeof(peers); /* a blob of older firmware is shorter and fits */
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
    int socks[CONNS];
    int n = 0;
    /* lwIP binds to an address no interface has yet, and the reset would leave the wrong way: wait until the
     * old address is up again, LAN can take 10 s */
    for (int round = 0; round < 100; round++) {
        bool waiting = false;
        for (int i = 0; i < CONNS; i++) {
            if (!peers[i].ip) {
                continue;
            }
            if (!db_net_path_ready(peers[i].local_ip, peers[i].ip)) {
                waiting = true;
                continue;
            }
            int sock = db_net_syn_from(peers[i].local_ip, htons(peers[i].local), peers[i].ip, peers[i].port);
            if (sock >= 0) {
                socks[n++] = sock;
                peers[i].ip = 0;
            } else {
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
    int missed = 0;
    for (int i = 0; i < CONNS; i++) {
        missed += peers[i].ip != 0;
    }
    ESP_LOGI(TAG, "ended %d connections of the last boot, %d hosts not reached", n, missed);
    for (int d = 0; n && d < DB_MAX_DECKS; d++) {
        announce_anew(d);
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

/* The Elgato App marks a dock offline when its connection dies, also when only the path in between was
 * down and the link stayed up, and comes back only for a new announcement. A fresh ARP answer shows the path
 * works again. */
static void rejoin_app(void *arg)
{
    struct tcpip_api_call_data call;
    tcpip_api_call(forget_arp, &call);
    for (int i = 0; i < 600; i++) {
        db_net_status_t n;
        db_net_status(&n);
        uint32_t from = n.lan_dead && n.wlan_up ? inet_addr(n.wlan_ip)
                                                : rejoin_local_ip; /* a dead LAN keeps its address */
        if (db_net_path_ready(from, rejoin_ip)) {
            announce_anew(rejoin_deck);
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
 * a new host gets in and is remembered while none is known or while pairing is open; otherwise it is turned
 * away and waits as a request that the deck or the web UI answers.
 */
#define KNOWN_MAX  8
#define NAMES_MAX  32
#define DENIED_MAX 4
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
    } names[NAMES_MAX]; /* RAM only: a name is asked again after a boot, it may have changed */
    int next_name;
    bool looking;                        /* one lookup task at a time */
    db_dock_denied_t denied[DENIED_MAX]; /* RAM only, oldest first */
    int n_denied;
} pair = {.mux = portMUX_INITIALIZER_UNLOCKED};

/* With the lock held. */
static void drop_denied(uint32_t ip)
{
    for (int i = 0; i < pair.n_denied; i++) {
        if (pair.denied[i].ip == ip) {
            memmove(&pair.denied[i], &pair.denied[i + 1],
                    (size_t)(--pair.n_denied - i) * sizeof(pair.denied[0]));
            return;
        }
    }
}

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
    for (int i = 0; i < NAMES_MAX; i++) {
        if (pair.names[i].ip == ip) {
            snprintf(pair.names[i].name, sizeof(pair.names[i].name), "%s", name);
        }
    }
    pair.looking = false;
    taskEXIT_CRITICAL(&pair.mux);
    if (name[0] && s.hosts_changed) {
        s.hosts_changed();
    }
    vTaskDelete(NULL);
}

/* Under the mux. Without an answer it asks again after a minute, not on every call. */
static bool should_ask(uint32_t ip, int64_t now)
{
    if (pair.looking) {
        return false;
    }
    for (int i = 0; i < NAMES_MAX; i++) {
        if (pair.names[i].ip == ip) {
            if (pair.names[i].name[0] || now - pair.names[i].asked_at < 60000000) {
                return false;
            }
            pair.names[i].asked_at = now;
            return pair.looking = true;
        }
    }
    int i = pair.next_name++ % NAMES_MAX;
    pair.names[i].ip = ip;
    pair.names[i].name[0] = 0;
    pair.names[i].asked_at = now;
    return pair.looking = true;
}

static void look_up(uint32_t ip)
{
    int64_t now = esp_timer_get_time();
    taskENTER_CRITICAL(&pair.mux);
    bool ask = should_ask(ip, now);
    taskEXIT_CRITICAL(&pair.mux);
    if (ask && xTaskCreate(lookup_task, "dock_name", 3072, (void *)(uintptr_t)ip, 2, NULL) != pdPASS) {
        taskENTER_CRITICAL(&pair.mux);
        pair.looking = false;
        taskEXIT_CRITICAL(&pair.mux);
    }
}

void db_dock_name(uint32_t ip, char name[DB_DNS_NAME_LEN])
{
    name[0] = 0;
    taskENTER_CRITICAL(&pair.mux);
    for (int i = 0; i < NAMES_MAX; i++) {
        if (pair.names[i].ip == ip) {
            snprintf(name, DB_DNS_NAME_LEN, "%s", pair.names[i].name);
        }
    }
    taskEXIT_CRITICAL(&pair.mux);
    if (!name[0] && ip) {
        look_up(ip); /* Companion over Satellite is no dock host, so nothing else asks for its name */
    }
}

static const uint8_t no_mac[6];

typedef struct {
    struct tcpip_api_call_data call;
    ip4_addr_t ip;
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

void db_dock_mac(uint32_t ip, uint8_t mac[6])
{
    arp_query_t q = {.ip.addr = ip};
    tcpip_api_call(arp_lookup, &q.call);
    memcpy(mac, q.mac, 6);
}

static esp_err_t save_known(void)
{
    db_dock_peer_t copy[KNOWN_MAX];
    taskENTER_CRITICAL(&pair.mux);
    memcpy(copy, pair.known, sizeof(copy));
    int n = pair.count;
    taskEXIT_CRITICAL(&pair.mux);
    nvs_handle_t h;
    esp_err_t err = nvs_open("dock", NVS_READWRITE, &h);
    if (err == ESP_OK) {
        err = nvs_set_blob(h, "hosts", copy, (size_t)n * sizeof(copy[0]));
        err = err == ESP_OK ? nvs_commit(h) : err;
        nvs_close(h);
    }
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "paired hosts not saved: %s", esp_err_to_name(err));
    }
    return err;
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
    db_dock_mac(ip, mac);
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
    bool new_request = !in && !asking;
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
    if (new_request) {
        char a[16];
        inet_ntoa_r(*(struct in_addr *)&ip, a, sizeof(a));
        db_log_event(TAG, "pairing", "%s asks to pair", a);
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

esp_err_t db_dock_answer(bool accept)
{
    taskENTER_CRITICAL(&pair.mux);
    uint32_t r = pair.request;
    pair.request = 0;
    bool added = accept && r && find(r, pair.request_mac) < 0 && remember(r, pair.request_mac, false);
    if (r) {
        drop_denied(r);
        if (!accept) {
            if (pair.n_denied == DENIED_MAX) {
                memmove(&pair.denied[0], &pair.denied[1], (DENIED_MAX - 1) * sizeof(pair.denied[0]));
                pair.n_denied--;
            }
            pair.denied[pair.n_denied++] = (db_dock_denied_t){r, esp_timer_get_time()};
        }
    }
    taskEXIT_CRITICAL(&pair.mux);
    esp_err_t err = added ? save_known() : ESP_OK;
    if (r) {
        char a[16];
        inet_ntoa_r(*(struct in_addr *)&r, a, sizeof(a));
        db_log_event(TAG, "pairing", "%s %s", a, accept ? "accepted" : "refused");
    }
    if (s.hosts_changed) {
        s.hosts_changed();
    }
    return err;
}

size_t db_dock_denied(db_dock_denied_t *out, size_t max)
{
    taskENTER_CRITICAL(&pair.mux);
    size_t n = (size_t)pair.n_denied < max ? (size_t)pair.n_denied : max;
    memcpy(out, pair.denied, n * sizeof(*out));
    taskEXIT_CRITICAL(&pair.mux);
    return n;
}

bool db_dock_undeny(uint32_t ip)
{
    taskENTER_CRITICAL(&pair.mux);
    int before = pair.n_denied;
    drop_denied(ip);
    bool dropped = pair.n_denied != before;
    taskEXIT_CRITICAL(&pair.mux);
    return dropped;
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
    for (int i = 0; i < CONNS; i++) {
        const conn_t *c = &conns[i];
        if (c->sock >= 0 && c->ip == p->ip) {
            return true;
        }
    }
    return false;
}

esp_err_t db_dock_forget(const char *ip)
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
    return save_known();
}

static bool satellite_host(uint32_t ip)
{
    const db_config_t *cfg = db_config();
    char host[sizeof(cfg->satellite)];
    db_sat_addr_host(cfg->satellite, host, sizeof(host));
    if (!host[0] || (cfg->controller == DB_CTRL_COMPANION && inet_addr(cfg->host) == ip)) {
        return false; /* a person picked this dock host */
    }
    return inet_addr(host) == ip || db_satellite_resolved() == ip || db_satellite_peer() == ip;
}

/* Companion that has the deck over Satellite would show it twice. Only Companion: an Elgato App on the same
 * machine gets in. */
static bool refuse(conn_t *c, db_controller_t kind)
{
    static uint32_t logged; /* once per host: Companion tries again every 5 s */
    if (kind != DB_CTRL_COMPANION || !satellite_host(c->ip)) {
        logged = logged == c->ip && kind == DB_CTRL_COMPANION ? 0 : logged;
        c->kind = kind;
        return false;
    }
    if (logged != c->ip) {
        logged = c->ip;
        char ip[16];
        inet_ntoa_r(*(struct in_addr *)&c->ip, ip, sizeof(ip));
        db_log_event(TAG, "source", "Companion at %s refused as a dock: it has the deck over Satellite", ip);
    }
    shutdown(c->sock, SHUT_RDWR);
    return true;
}

void db_dock_drop_satellite_host(void)
{
    for (int i = 0; i < CONNS; i++) {
        conn_t *c = &conns[i];
        if (!c->tx) {
            return; /* before db_dock_start */
        }
        xSemaphoreTake(c->tx, portMAX_DELAY);
        if (c->sock >= 0 && c->kind == DB_CTRL_COMPANION && satellite_host(c->ip)) {
            shutdown(c->sock, SHUT_RDWR);
        }
        xSemaphoreGive(c->tx);
    }
}

static void accept_host(int ls, int deck, bool device_port)
{
    uint16_t port = port_of(deck, device_port);
    struct sockaddr_in from;
    socklen_t len = sizeof(from);
    int sock = accept(ls, (struct sockaddr *)&from, &len);
    if (sock < 0) {
        ESP_LOGW(TAG, "port %u: accept failed: %s", port, strerror(errno));
        return;
    }
    conn_t *c = NULL;
    for (int i = 0; i < DB_DOCK_HOSTS && !c; i++) {
        c = conn(deck, device_port, i)->sock < 0 ? conn(deck, device_port, i) : NULL;
    }
    char ip[16];
    inet_ntoa_r(from.sin_addr, ip, sizeof(ip));
    if (!admit(from.sin_addr.s_addr)) {
        ESP_LOGW(TAG, "port %u: %s is not paired, asking on the deck", port, ip);
        close(sock);
        return;
    }
    if (!c) {
        ESP_LOGW(TAG, "port %u: %s refused, %d hosts connected", port, ip, DB_DOCK_HOSTS);
        close(sock);
        return;
    }
    ESP_LOGI(TAG, "%s host %s on port %u", device_port ? "device" : "dock", ip, port);
    c->ip = from.sin_addr.s_addr;
    c->peer_port = from.sin_port;
    struct sockaddr_in at;
    socklen_t at_len = sizeof(at);
    c->local_ip = getsockname(sock, (struct sockaddr *)&at, &at_len) == 0 ? at.sin_addr.s_addr : 0;
    c->kind = DB_CTRL_SERVER;
    c->sock = sock;
    if (xTaskCreatePinnedToCore(host_task, device_port ? "dock_dev" : "dock_host", 4096, c,
                                device_port ? 7 : 6, NULL, 0) != pdPASS) {
        ESP_LOGE(TAG, "no memory for a host task, %s refused", ip);
        close(sock);
        c->sock = -1;
    }
}

static void listen_task(void *arg)
{
    int ls[DB_MAX_DECKS * 2];
    memset(ls, -1, sizeof(ls));
    for (;;) {
        fd_set ready;
        FD_ZERO(&ready);
        int top = -1;
        for (int i = 0; i < DB_MAX_DECKS * 2; i++) {
            /* a socket each, of 40 in all; a deck over a lowered limit keeps its ports until it goes */
            if (i / 2 >= db_config()->max_decks && !db_deck_present(i / 2)) {
                if (ls[i] >= 0) {
                    close(ls[i]);
                    ls[i] = -1;
                }
                continue;
            }
            if (ls[i] < 0) {
                ls[i] = listen_on(port_of(i / 2, i % 2), DB_DOCK_HOSTS);
            }
            if (ls[i] >= 0) {
                FD_SET(ls[i], &ready);
                top = ls[i] > top ? ls[i] : top;
            }
        }
        struct timeval wait = {.tv_sec = 1}; /* a port that could not listen is tried again */
        if (top < 0 || select(top + 1, &ready, NULL, NULL, &wait) <= 0) {
            vTaskDelay(pdMS_TO_TICKS(top < 0 ? 1000 : 0));
            continue;
        }
        for (int i = 0; i < DB_MAX_DECKS * 2; i++) {
            if (ls[i] >= 0 && FD_ISSET(ls[i], &ready)) {
                accept_host(ls[i], i / 2, i % 2);
            }
        }
    }
}

static char device_instance[DB_MAX_DECKS][32];

static esp_timer_handle_t services_timer;
static uint32_t services_due; /* decks whose services sync_services renews; under info_lock */

static void reannounce(int deck)
{
    xSemaphoreTakeRecursive(mdns_lock, portMAX_DELAY);
    if (!dock_instance[deck][0]) {
        if (!esp_timer_is_active(services_timer)) { /* a deck that just came waits for its debounce */
            announce(deck);
        }
        xSemaphoreGiveRecursive(mdns_lock);
        return;
    }
    /* setting the unchanged port is the API's only way to send the full service announcement again */
    mdns_service_port_set_for_host(dock_instance[deck], "_elg", "_tcp", NULL, port_of(deck, false));
    xSemaphoreGiveRecursive(mdns_lock);
    xSemaphoreTake(s.info_lock, portMAX_DELAY);
    if (device_instance[deck][0]) {
        mdns_service_port_set_for_host(device_instance[deck], "_elg", "_tcp", NULL, port_of(deck, true));
    }
    xSemaphoreGive(s.info_lock);
}

static void keepalive_task(void *arg)
{
    uint8_t buf[DB_CORA_REPORT_LEN];
    for (int tick = 1;; tick++) {
        vTaskDelay(pdMS_TO_TICKS(KEEPALIVE_MS));
        if (tick % (ANNOUNCE_MS / KEEPALIVE_MS) == 0) {
            for (int d = 0; d < DB_MAX_DECKS; d++) {
                reannounce(d);
            }
        }
        if (__atomic_exchange_n(&peers_due, false, __ATOMIC_RELAXED)) {
            keep_peers();
        }
        db_net_status_t n;
        db_net_status(&n);
        uint32_t wlan = n.wlan_up && n.lan_up && !n.lan_dead ? inet_addr(n.wlan_ip) : 0;
        for (int i = 0; i < CONNS; i++) {
            conn_t *c = &conns[i];
            if (c->sock >= 0 && wlan && c->local_ip == wlan) {
                shutdown(c->sock,
                         SHUT_RDWR); /* came over while the LAN was dead: back to LAN via the rejoin */
            } else if (c->sock >= 0) {
                db_cora_keepalive(c->connection_no, buf);
                send_frame_to(c, 0, 0, 0, buf, sizeof(buf),
                              false); /* one stuck host must not delay the others */
            }
        }
    }
}

/* Input goes to the host that draws, so presses in the menu stay in the menu. */
static void on_raw_input(int deck, const uint8_t *report, size_t len)
{
    uint8_t buf[DB_CORA_REPORT_LEN] = {0}; /* the dock forwards full 512 byte input reports */
    memcpy(buf, report, len < sizeof(buf) ? len : sizeof(buf));
    db_source_t writer = db_deck_writer(deck);
    for (int i = 0; i < DB_DOCK_HOSTS; i++) {
        conn_t *c = conn(deck, true, i);
        if (c->sock >= 0 && c->kind != DB_CTRL_SERVER && source_of(c) == writer &&
            db_deck_input_real_for(c->ip)) {
            send_frame(c, DB_CORA_FLAG_VERBATIM, 0, 0, buf, sizeof(buf));
        }
    }
}

/* The attached device is also announced as its own service on the device port. */
static void device_service(int deck, const db_device_info_t *info)
{
    char *instance = device_instance[deck];
    if (instance[0]) {
        mdns_service_remove_for_host(instance, "_elg", "_tcp", NULL);
        instance[0] = '\0';
    }
    if (!info) {
        return;
    }
    char vid[8];
    char pid[8];
    snprintf(vid, sizeof(vid), "0x%04X", info->vid);
    snprintf(pid, sizeof(pid), "0x%04X", info->pid);
    mdns_txt_item_t txt[] = {
        {"dt", "213"}, {"mf", "Elgato"},       {"md", db_cora_model(info->pid)},
        {"pv", "1.0"}, {"sn", info->serial},   {"vid", vid},
        {"pid", pid},  {"fw", info->firmware}, {"prt", "2"},
    };
    strlcpy(instance, info->serial, sizeof(device_instance[deck]));
    esp_err_t err = mdns_service_add_for_host(instance, "_elg", "_tcp", NULL, port_of(deck, true), txt,
                                              sizeof(txt) / sizeof(txt[0]));
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "mDNS device announcement failed: %s", esp_err_to_name(err));
        instance[0] = '\0';
    }
}

/* Unasked 0x1c reports tell connected hosts that a device came or went; they then open or close the device
 * port. The Elgato App drops a dock that had no device for it and only comes back when the service goes
 * offline and online again, so the service is announced anew as well. */
static void tell_hosts(int deck)
{
    for (int i = 0; i < DB_DOCK_HOSTS; i++) {
        conn_t *c = conn(deck, false, i);
        if (c->sock >= 0 && c->kind != DB_CTRL_SERVER) {
            send_dock_report(c, 0x1c, 0);
        }
    }
}

static void sync_services(void *arg)
{
    xSemaphoreTake(s.info_lock, portMAX_DELAY);
    uint32_t due = services_due;
    services_due = 0;
    for (int d = 0; d < DB_MAX_DECKS; d++) {
        if (due & 1u << d) {
            device_service(d, s.info[d].device);
        }
    }
    xSemaphoreGive(s.info_lock);
    xSemaphoreTakeRecursive(mdns_lock, portMAX_DELAY);
    for (int d = 0; d < DB_MAX_DECKS; d++) {
        if (due & 1u << d) {
            withdraw(d);
            announce(d);
        }
    }
    xSemaphoreGiveRecursive(mdns_lock);
}

/* The services wait until the deck was there or gone for a second: a deck that comes and goes fast made them
 * go offline and online several times, and the Elgato App then never resolved the device again. Only the
 * deck that changed: the others' hosts must not see their dock go. */
static void on_device(int deck, const db_device_info_t *info)
{
    info = info && info->dock ? info : NULL;
    xSemaphoreTake(s.info_lock, portMAX_DELAY);
    if (info) {
        attached[deck] = *info;
    }
    s.info[deck].device = info ? &attached[deck] : NULL;
    services_due |= 1u << deck;
    xSemaphoreGive(s.info_lock);
    tell_hosts(deck);
    esp_timer_stop(services_timer);
    esp_timer_start_once(services_timer, 1000000);
}

size_t db_dock_hosts(int deck, db_dock_host_t *out, size_t max)
{
    size_t n = 0;
    for (int d = deck < 0 ? 0 : deck; d < (deck < 0 ? DB_MAX_DECKS : deck + 1); d++) {
        for (int i = 0; i < DB_DOCK_HOSTS && n < max; i++) {
            const conn_t *c = conn(d, false, i);
            if (!c->tx || c->sock < 0) { /* no tx: before db_dock_start, when sock is a zero, not a socket */
                continue;
            }
            int source = -1;
            for (int k = 0; k < DB_DOCK_HOSTS; k++) {
                const conn_t *dev = conn(d, true, k);
                source = dev->sock >= 0 && dev->ip == c->ip ? source_of(dev) : source;
            }
            out[n] = (db_dock_host_t){.kind = c->kind, .deck = d, .source = source};
            struct in_addr a = {.s_addr = c->ip};
            inet_ntoa_r(a, out[n].ip, sizeof(out[n].ip));
            n++;
        }
    }
    return n;
}

/* Only with a deck: the Elgato App rejects a dock without one and then ignores it, also once a deck is back,
 * until the app starts again. A dock that appears with its deck is new to it. */
static void announce(int deck)
{
    xSemaphoreTake(s.info_lock, portMAX_DELAY);
    const db_cora_dock_t *info = &s.info[deck];
    bool present = info->device != NULL;
    xSemaphoreGive(s.info_lock);
    xSemaphoreTakeRecursive(mdns_lock, portMAX_DELAY);
    char *instance = dock_instance[deck];
    if (!present || instance[0]) {
        xSemaphoreGiveRecursive(mdns_lock);
        return;
    }
    snprintf(instance, sizeof(dock_instance[deck]), "Stream Deck Network Dock %02X%02X%02X", info->mac[3],
             info->mac[4], info->mac[5]);
    char id[18];
    snprintf(id, sizeof(id), "%02X:%02X:%02X:%02X:%02X:%02X", info->mac[0], info->mac[1], info->mac[2],
             info->mac[3], info->mac[4], info->mac[5]);
    mdns_txt_item_t txt[] = {
        {"dt", "215"},
        {"mf", "Elgato"},
        {"md", "Stream Deck Network Dock 20GBW9901"},
        {"pv", "1.0"},
        {"id", id},
        {"sn", info->serial},
        {"fw", DB_CORA_FIRMWARE},
        {"hwrev", "0200"},
    };
    esp_err_t err =
        mdns_service_add(instance, "_elg", "_tcp", port_of(deck, false), txt, sizeof(txt) / sizeof(txt[0]));
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "mDNS announcement failed: %s", esp_err_to_name(err));
        instance[0] = '\0'; /* the next re-announce tries again */
    }
    xSemaphoreGiveRecursive(mdns_lock);
}

static void withdraw(int deck)
{
    xSemaphoreTakeRecursive(mdns_lock, portMAX_DELAY);
    if (dock_instance[deck][0]) {
        mdns_service_remove_for_host(dock_instance[deck], "_elg", "_tcp", NULL);
        dock_instance[deck][0] = '\0';
    }
    xSemaphoreGiveRecursive(mdns_lock);
}

/* Runs in esp_restart(). Hosts must see the dock go: the connections end with a reset, and mDNS says goodbye,
 * or the Elgato App keeps its cached dock and does not come back after a short restart. */
static void close_hosts(void)
{
    mdns_service_remove_all();
    for (int i = 0; i < CONNS; i++) {
        int sock = conns[i].sock;
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
    mdns_lock = xSemaphoreCreateRecursiveMutex();
    load_known();
    if (xTaskCreate(reset_old_peers, "dock_reset", 3072, NULL, 3, NULL) != pdPASS) {
        ESP_LOGW(TAG, "no task to end the connections of the last boot");
    }
    esp_register_shutdown_handler(close_hosts);
    s.info_lock = xSemaphoreCreateMutex();
    if (!s.info_lock) {
        return ESP_ERR_NO_MEM;
    }
    for (int d = 0; d < DB_MAX_DECKS; d++) {
        /* the first deck's dock keeps the bridge's MAC and serial; the others get local ones of their own, or
         * the Elgato App would take them for one dock */
        db_cora_dock_t *info = &s.info[d];
        info->device_port = port_of(d, true);
        esp_read_mac(info->mac, ESP_MAC_BASE);
        if (d) {
            info->mac[0] |= 0x02;
            info->mac[5] = (uint8_t)(info->mac[5] + d);
        }
        snprintf(info->serial, sizeof(info->serial), "D%c%02X%02X%02X%02X%02X%02X", d ? '1' + d : 'B',
                 info->mac[0], info->mac[1], info->mac[2], info->mac[3], info->mac[4], info->mac[5]);
        for (int i = 0; i < DB_DOCK_HOSTS; i++) {
            *conn(d, false, i) =
                (conn_t){.deck = d, .host = i, .connection_no = 1, .sock = -1, .tx = xSemaphoreCreateMutex()};
            *conn(d, true, i) = (conn_t){.deck = d,
                                         .host = i,
                                         .device_port = true,
                                         .connection_no = 2,
                                         .sock = -1,
                                         .tx = xSemaphoreCreateMutex()};
        }
    }
    esp_timer_create_args_t timer = {.callback = sync_services, .name = "dock_mdns"};
    esp_timer_create(&timer, &services_timer);
    for (int d = 0; d < DB_MAX_DECKS; d++) {
        if (db_deck_info(d, &attached[d]) && attached[d].dock) {
            s.info[d].device = &attached[d];
            device_service(d, &attached[d]);
        }
    }
    db_deck_listener_t l = {.device_changed = on_device, .raw_input = on_raw_input, .external = true};
    db_deck_add_listener(&l);
    if (xTaskCreatePinnedToCore(listen_task, "dock_port", 3072, NULL, 7, NULL, 0) != pdPASS ||
        xTaskCreatePinnedToCore(keepalive_task, "dock_ka", 3072, NULL, 5, NULL, 0) != pdPASS) {
        return ESP_ERR_NO_MEM;
    }
    for (int d = 0; d < DB_MAX_DECKS; d++) {
        announce(d);
    }
    ESP_LOGI(TAG, "Network Dock emulation from port %d, two per deck", DB_CORA_DOCK_PORT);
    return ESP_OK;
}
