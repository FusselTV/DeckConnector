#pragma once

#include "db_driver.h"

/*
 * A virtual Mirabox Stream Dock behind a db_hid_io_t, built from mirabox_ref.h (what Bitfocus' plugin sends),
 * never from the driver. It checks every report and hands each shown image to the sink.
 */

#define MIRABOX_EMU_DECKS 4

/* id: the picture id of the key; data: the JPEG as sent. */
extern void (*mirabox_emu_sink)(int deck, uint8_t id, const uint8_t *data, size_t len);

bool mirabox_emu_start(int deck, uint16_t vid, uint16_t pid);
const char *mirabox_emu_name(int deck);
const db_hid_io_t *mirabox_emu_io(int deck);
int mirabox_emu_brightness(int deck); /* the byte of the last LIG, -1 before */
/* The input report for a press (state 1), release (0) or turn (-1 left, 1 right) of the control at row,
 * col. 0 when it has no such input, MIRABOX_EMU_NONE for the release of a control that reports only presses.
 */
#define MIRABOX_EMU_NONE ((size_t)-1)
size_t mirabox_emu_input(int deck, uint8_t row, uint8_t col, bool turn, int state, uint8_t *out, size_t cap);
