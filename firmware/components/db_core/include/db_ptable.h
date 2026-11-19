#pragma once

#include <stddef.h>
#include <stdint.h>

/* Flash between phy_init and ota_0 that no partition may use: Matter keeps its pairings there. */
#define DB_SPARE_OFFSET 0x12000
#define DB_SPARE_SIZE   0xE000

/* Whether partition table `next` may replace `now` (both as written at 0x8000, `len` bytes): every data
 * partition stays where it is with its size, the running app keeps its place, every app slot still fits
 * `image_len`, nothing overlaps or ends past `flash_size`. NULL when it may, else the reason. The MD5 entry
 * is checked by the caller. */
const char *db_ptable_check(const uint8_t *now, const uint8_t *next, size_t len, uint32_t running_offset,
                            uint32_t image_len, uint32_t flash_size);
