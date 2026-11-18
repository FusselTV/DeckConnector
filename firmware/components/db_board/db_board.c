#include "db_board.h"

#include "sdkconfig.h"

#if CONFIG_DB_BOARD_WAVESHARE_P4_POE_ETH
/* Waveshare ESP32-P4-WIFI6-POE-ETH: IP101 PHY, ESP32-C6 over SDIO, USB-A on the high speed PHY. */
static const db_board_t board = {
    .name = "waveshare-p4-poe-eth",
    .eth = {.present = true, .mdc_gpio = 31, .mdio_gpio = 52, .phy_reset_gpio = 51, .phy_addr = 1},
    .wifi = true,
    .speaker = {.present = true,
                .sda_gpio = 7,
                .scl_gpio = 8,
                .mclk_gpio = 13,
                .bclk_gpio = 12,
                .ws_gpio = 10,
                .dout_gpio = 9,
                .din_gpio = 11,
                .pa_gpio = 53},
};
#elif CONFIG_DB_BOARD_SIM
static const db_board_t board = {.name = "sim", .eth = {.present = true}};
#else
#error "No board selected, see menuconfig -> DeckConnector board"
#endif

const db_board_t *db_board(void)
{
    return &board;
}
