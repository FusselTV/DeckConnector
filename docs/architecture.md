# Architecture

```
firmware/
  main/              composition root: starts components, connects their hooks
  components/
    db_core/         plain C, no ESP-IDF: device model, drivers, image queue, protocol, Cora framing
    db_board/        pins and features per board
    db_config/       settings in NVS, JSON for the web UI
    db_deck/         USB host port and the deck service (who may draw, writer task, input)
    db_net/          Ethernet, WLAN, setup access point, mDNS
    db_link/         WebSocket client and the connection to one or two servers
    db_ui/           status screens and menu on the device, font, JPEG encoder
    db_ota/          signed updates for P4 and C6, rollback
    db_web/          web UI, JSON API, captive DNS
    db_dock/         Network Dock emulation
    db_log/          log ring buffer for the web UI
host_test/           Unity tests for db_core, built with the host compiler
tools/dbtools/       reference server, benchmarks, signing, font generator, serial log
```

## Layers

- **db_core** knows no chip. It compiles on a PC and its tests run with AddressSanitizer.
- **Drivers** speak one USB protocol family. They get a `db_hid_io_t` and fill a `db_device_info_t`. A new model
  of a known family is one table row. A new vendor is one driver file and one line in `db_driver.c`.
- **Ports** wrap the chip: USB host (`db_deck/usb_port.c`), JPEG (`db_ui/jpeg_port.c`), network, storage.
- **Boards** are one struct in `db_board.c`, picked in menuconfig.

## Who draws

Exactly one source writes to the device at a time. `db_ui` decides:

1. While the menu is open or an update runs, the device UI draws.
2. In Network Dock mode, the dock host draws once it opened the device port.
3. In native mode, the active server draws.

In every other case the device UI draws.

The device UI then shows what is missing, for example "Kein Netz" or "Suche Server".

## Data path

Server image → `db_link` reads the frame straight into one PSRAM buffer → `db_deck_submit` drops every pending
image the new one covers → the writer task sends it over USB → the server gets an ACK with bridge timestamps.

Input runs the other way: USB report → driver → one event per change → the active server.
