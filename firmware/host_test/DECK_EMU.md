# Deck emulator

`deck_emu.c` is a virtual Stream Deck behind a `db_hid_io_t`. `test_decks.c` drives every accepted PID
through `db_driver_find()` against it. All expectations come from Elgato's HID pages, never from the driver code.

## What the emulator checks

- Output reports are 1024 bytes, report id `0x02`, a command the model has. Classic and XL take `0x07`
  and `0x08`. Neo, + and + XL also take `0x0b` and `0x0c`. Six-key decks take only `0x01`. The Pedal
  takes no image at all.
- Gen2 headers: key index in range, reserved bytes zero, done flag 0 or 1, size field 1..1016 (1..1008 for
  `0x0c`), zero padding after the data. `0x0c` uses its own 16-byte header, and its rect must lie inside
  the window.
- Six-key headers: chunk index, byte 3 zero, last-chunk flag, key index plus one (1..6), bytes 6..15 zero.
- Every transfer: chunk index starts at 0 and counts up, done flag only on the last chunk, command, key
  and rect stay the same, no new image before the last one finished, no transfer left open.
- Every image: JPEG starts `FFD8`, ends `FFD9`, and its SOF0 size is the target size after the model's
  rotation (90 and 270 swap width and height). Six-key images are BMPs of 80 x 80, 24 bits, with zero
  padding after the BMP's own size.
- SET_FEATURE is 32 bytes, a setter the family knows, values in range, zero padding. Brightness is
  `03 08 pct` on gen2 and `05 55 aa d1 01 pct` on six-key.
- GET_FEATURE answers only the reports the family has. Gen2: `0x08` unit info (window size at `0x11` and
  `0x13`), `0x04`/`0x05`/`0x07` firmware, `0x06` serial, `0x0a` sleep. Six-key: `0xa0`..`0xa2`
  firmware, `0x03` serial, `0xa3` sleep. Any other report is a violation.

`test_decks` then checks the device info (key, encoder and touch counts, format, rotation, targets), writes
an image to every target and a sub-rectangle to partial strips, sets brightness, feeds deck input
reports to `parse_input`, compares `build_input` with the report the deck sends, and reads every image
back with `parse_output` when the driver has it.

## Running

```
export PATH=$HOME/.espressif/tools/cmake/4.0.3/bin:$HOME/.espressif/tools/ninja/1.12.1:$PATH
cmake -S firmware/host_test -B build-host -G Ninja && cmake --build build-host
./build-host/test_decks                    # all 16 PIDs, then a + and a Mini at once
DECK_PIDS=84,c6 ./build-host/test_decks    # a subset, hex, 0x prefix optional
```

Each PID runs in its own process, so a crash fails only that PID. The run ends with a PASS, FAIL or CRASH
line per PID and exits non-zero if any PID fails.

## Assumptions

The spec leaves these open. A failure here may be the emulator's fault, not the driver's.

- Pedal input is the gen2 key report with 3 bytes. Its `0x08` answer is 1 row x 3 columns with zero image
  sizes. It takes setters `03 02`, `03 08` and `03 0d`. The driver sends it no brightness, as the Elgato
  dock sends it no display reports.
- Six-key BMPs are 24 bits per pixel with a BITMAPINFOHEADER. Row order is not checked.
- Six-key chunks have no size field, so every chunk carries 1008 bytes and the BMP size field marks the end.
- The Neo has `key_count` 10: 8 image keys plus the two sensors as input keys 8 and 9. Sensors get no image
  target, because the spec gives no command for the 96 x 16 sensor image.
- A full LCD target is optional on every model with an LCD except the +, because no source gives where the
  keys sit on the panel. When a driver offers one, it must match the LCD size.
- The + XL sends a tap with payload length `0x10`, as its Elgato page says. Long press stays `0x0a`.
- The touch area must equal the window target. Touch events are window coordinates plus the touch area's
  origin.
- Unit info fields the spec gives no value for (bpp, color scheme, gallery counts) are zero.
