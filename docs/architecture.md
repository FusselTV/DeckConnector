# Architecture

```
firmware/
  main/              starts the components and connects their hooks
  components/
    db_core/         plain C, no ESP-IDF: device model, drivers, image queue, protocol, Cora framing, Satellite
                     lines, layout and PNG
    db_board/        pins and features per board
    db_config/       settings in NVS, JSON for the web UI
    db_deck/         USB host port, JPEG encoder, and the deck service (who may draw, writer task, input)
    db_net/          Ethernet, WLAN, setup access point, mDNS
    db_link/         WebSocket client, the connections to the servers, and the Companion Satellite client
    db_ui/           status screens and menu on the device, font
    db_ota/          signed updates for P4 and C6, rollback
    db_web/          web UI, JSON API, captive DNS
    db_dock/         Network Dock emulation
    db_log/          log ring buffer for the web UI
host_test/           Unity tests for db_core, built with the host compiler
firmware/sim/        db_web and db_config on Linux with fakes for the chip, for the API tests
tools/dbtools/       reference server, benchmarks, signing, font generator, serial log
```

## Layers

- **db_core** knows no chip. It compiles on a PC and its tests run with AddressSanitizer.
- **Drivers** speak one USB protocol family. They get a `db_hid_io_t` and fill a `db_device_info_t`. A new model
  of a known family is one table row. A new vendor is one driver file and one line in `db_driver.c`.
- **Ports** wrap the chip: USB host (`db_deck/usb_port.c`), JPEG (`db_deck/jpeg_port.c`), network, storage.
- **Boards** are one struct in `db_board.c`, picked in menuconfig.

## Who draws

Exactly one source writes to the device at a time. `db_ui` decides:

1. While the menu is open or an update runs, the device UI draws.
2. Otherwise the controller from the settings draws. That is the active server, the Elgato App or
   Companion once it opened the device port, or Companion over its Satellite API once it took the deck.

In every other case the device UI draws its home screen. It shows what is missing, for example "Kein Netz" or
"Warte auf Companion", and lets you pick the controller there.

The device UI keeps what it last drew and only draws and sends the keys and strip parts that changed. After
another source drew, or when more than five areas changed, it sends the whole screen as one image.

`db_deck` keeps the newest image per area for every source (`db_core/db_screen.c`). Dock hosts send raw reports,
which the driver reads back into images. Companion over its Satellite API draws into its kept picture also
while it is not the writer. When a source other than the device UI becomes the writer again, `db_deck`
draws its kept picture at once, before anything new. `GET /api/screen` serves the writer's kept picture.

## Data path

Server image → `db_link` reads the frame straight into one PSRAM buffer → `db_deck_submit` drops every pending
image the new one covers → the writer task sends it over USB → the server gets an ACK with DeckConnector
timestamps.

Input runs the other way: USB report → driver → one event per change → the active server.
