#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* A reverse DNS (PTR) lookup over UDP: routers answer it with the name a host gave with its DHCP request. */

#define DB_DNS_NAME_LEN 40

/* `ip` in network order. Returns the query length; `out` needs 64 bytes. */
size_t db_dns_ptr_query(uint32_t ip, uint16_t id, uint8_t *out);
/* The host part of the first PTR answer ("studio-pc" of "studio-pc.lan."). False when there is none. */
bool db_dns_ptr_name(const uint8_t *msg, size_t len, uint16_t id, char name[DB_DNS_NAME_LEN]);
