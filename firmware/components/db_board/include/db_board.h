#pragma once

#include <stdbool.h>
#include <stdint.h>

/* Everything that differs between boards. Pick the board in menuconfig (Deck Connector board). */
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
    struct {   /* an ES8311 codec, its microphone and its amplifier */
        bool present;
        int sda_gpio, scl_gpio; /* the codec's I2C control, address 0x18 */
        int mclk_gpio, bclk_gpio, ws_gpio, dout_gpio, din_gpio;
        int pa_gpio; /* the amplifier's enable */
    } speaker;
} db_board_t;

const db_board_t *db_board(void);
