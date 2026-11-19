# Firmware

The ESP-IDF project for the ESP32-P4. Build, test and flash commands are in
[CONTRIBUTING](../.github/CONTRIBUTING.md).

| Folder | What it is |
|---|---|
| `main/` | start-up, and the device part of `/api/status` |
| `components/db_core` | device model, deck drivers in `drivers/`, image queue, protocol, Cora framing, Satellite lines, layout and PNG, the event flash ring, in plain C without ESP-IDF |
| `components/db_board` | pins and features per board, picked in menuconfig |
| `components/db_config` | settings in NVS |
| `components/db_deck` | who may draw, the writer task and input in `db_deck.c`, USB host in `usb_port.c`, JPEG encoder in `jpeg_port.c` |
| `components/db_net` | Ethernet, Wi-Fi, setup access point, mDNS, captive DNS |
| `components/db_link` | own servers over WebSocket in `db_link.c` and `ws.c`, the Companion Satellite client in `sat.c` |
| `components/db_dock` | the Network Dock emulation |
| `components/db_ui` | deck UI state and task in `db_ui.c`, pages and menu in `screens.c`, tiles in `render.c`, pixels and text in `canvas.c`, the generated font in `font_data.c` |
| `components/db_ota` | signed updates and rollback, the `.dbfw` header in `db_ota.c` |
| `components/db_web` | routes and server in `http.c`, JSON API in `api.c`, login and tokens in `auth.c`, TLS in `cert.c`, live events in `events.c`, metrics in `metrics.c` |
| `components/db_audio` | the speaker, its sounds and the self-test |
| `components/db_log` | the log ring kept over a restart, boot time marks, the event history in `evlog.c` |
| `components/db_serial` | Improv over USB serial, for the flasher |
| `components/db_matter` | Matter, off until turned on. Each deck is a bridged node, each key and dial press a switch. `CMakeLists.txt` fetches and patches esp-matter |
| `host_test/` | db_core and the deck screens on a PC, with a [deck emulator](host_test/DECK_EMU.md) |
| `sim/` | db_web, db_config and db_core on Linux, with fakes for the rest. It runs the board's mDNS responder |
| `simdeck/` | the firmware's own stack on Linux, on emulated decks. `components/esp_matter` stands in for the Matter stack and logs what a controller would see |
| `vendor/` | Espressif's USB host and mDNS components, kept in the tree |
