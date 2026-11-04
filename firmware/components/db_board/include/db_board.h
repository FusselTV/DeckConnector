#pragma once

#include <stdbool.h>
#include <stdint.h>

/* Everything that differs between boards. Pick the board in menuconfig (Deckbridge board). */
typedef struct {
    const char *name;
    struct {
        bool present;
        int mdc_gpio;
        int mdio_gpio;
        int phy_reset_gpio;
        int phy_addr;
    } eth;
    bool wifi; /* native radio or ESP-Hosted co-processor */
} db_board_t;

const db_board_t *db_board(void);
