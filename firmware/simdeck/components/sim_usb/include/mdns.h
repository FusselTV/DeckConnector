/* No mDNS in the simulator: nothing is announced and no server is found. Hosts connect by address. */
#pragma once

#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"
#include "lwip/sim.h"

typedef struct {
    const char *key;
    const char *value;
} mdns_txt_item_t;

typedef struct {
    struct {
        struct {
            ip4_addr_t ip4;
        } u_addr;
    } addr;
} mdns_ip_addr_t;

typedef struct mdns_result_s {
    struct mdns_result_s *next;
    char *instance_name;
    uint16_t port;
    mdns_ip_addr_t *addr;
} mdns_result_t;

#define IPSTR          "%d.%d.%d.%d"
#define IP2STR(ipaddr) 0, 0, 0, 0

static inline esp_err_t mdns_query_ptr(const char *s, const char *p, uint32_t t, size_t max,
                                       mdns_result_t **r)
{
    return ESP_ERR_NOT_FOUND;
}
static inline void mdns_query_results_free(mdns_result_t *r)
{
}
static inline esp_err_t mdns_service_add(const char *i, const char *s, const char *p, uint16_t port,
                                         mdns_txt_item_t *txt, size_t n)
{
    return ESP_OK;
}
static inline esp_err_t mdns_service_add_for_host(const char *i, const char *s, const char *p, const char *h,
                                                  uint16_t port, mdns_txt_item_t *txt, size_t n)
{
    return ESP_OK;
}
static inline esp_err_t mdns_service_port_set_for_host(const char *i, const char *s, const char *p,
                                                       const char *h, uint16_t port)
{
    return ESP_OK;
}
static inline esp_err_t mdns_service_remove_for_host(const char *i, const char *s, const char *p,
                                                     const char *h)
{
    return ESP_OK;
}
static inline esp_err_t mdns_service_remove_all(void)
{
    return ESP_OK;
}
