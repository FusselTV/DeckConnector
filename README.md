# DeckConnector

Firmware for an ESP32-P4 board that drives an Elgato deck over USB and puts it on the network. Your own
server renders the images and gets every key press, encoder turn and touch. The board also acts like an Elgato
Network Dock, so Bitfocus Companion and the Elgato App use it without any extra software. Companion can
also take the deck over its Satellite API. Every connected host is in one list, on the deck and in the web UI. A
tap picks the one that draws, and the deck shows its picture at once.

## What it does

- One key takes 0.5 ms from the server to the end of the USB transfer, a 9 KB full screen image 1.7 ms (LAN,
  Stream Deck +).
- LAN and WLAN at the same time. Connections go over LAN. If a server cannot be reached there, the next try
  goes over WLAN. As soon as the server answers over LAN again, the connection moves back. A dead LAN shows within
  6 s. The Network Dock is announced on WLAN only while the LAN has no address.
- Up to four servers. The first connected one in the list draws. If it goes away, the next one takes over, at
  once when the connection closes and within 6 s when the server goes silent.
- The newest image for each area wins, so a slow link never builds a queue.
- `wss://` for servers and for Companion's WebSocket port, checked against public CAs or a pinned certificate.
- Signed updates for the firmware and the WLAN chip. New firmware that does not come up rolls back by
  itself.
- A status screen and a settings menu on the device itself, and a setup WLAN for first use.
- A web UI with a live view of the deck. You can click, hold, swipe and scroll it like the real one.
- A JSON API for all of it, see [docs/api.md](docs/api.md).

## Hardware

- Waveshare ESP32-P4-WIFI6-POE-ETH (ESP32-P4 with an ESP32-C6 for WLAN, PoE, USB-A on the high speed PHY).
- Stream Deck + (tested). Stream Deck MK.2 and XL are in the driver table but not tested yet.

The board powers the deck from its USB-A port. With PoE the whole setup runs from one cable.

## Build and flash

Needs ESP-IDF 6.0.2, Node.js and [uv](https://docs.astral.sh/uv/).

```
git clone https://github.com/<owner>/deckbridge.git
```

The firmware carries the web UI, so build it first. Then make your own update key once. The firmware takes only
updates signed with the key it was built with.

```
cd deckbridge/web && npm ci && npm run build
cd ../tools && uv run python -m dbtools.dbfw keygen
cd ../firmware && idf.py build && idf.py -p /dev/ttyACM0 flash
```

The first start creates a password for the web UI. The device shows it in its menu, on the strip under
Web-Zugang.

## Setup

1. Plug in LAN, or wait a minute. Without a network the board opens a setup WLAN. Its name and password are on
   the device.
2. Open `http://<address>/` and log in. The setup wizard starts.
3. Connect the program that should draw. Later you pick it under Active source on the overview.
   - Companion, best over its Satellite API. Enter the Companion address in the wizard or under Sources. The
     board then connects to Companion itself. This also works when Companion runs in Docker
     without host networking, as long as port 16622, or 16623 for `ws://`, is reachable. Without it, the board
     shows up in Companion under Surfaces, Discover, as a Network Dock. When Companion connects that way and its
     Satellite API answers, the board moves it to Satellite by itself.
   - Elgato App. It finds the board by itself.
   - Your own servers. Add a server URL (`ws://` or `wss://`). Without one, the board finds servers that
     announce `_deckconnector._tcp` over mDNS.

When nobody draws, the deck shows its home screen. Its top row lists the connected hosts. Tap one twice to let
it draw, or Menü for the settings. While a host draws, hold the first and the last encoder for two seconds to
open the menu. When it closes, the deck shows the host's picture again at once.

### Without the web UI

The web UI is optional. Plug in LAN. The deck shows its address and how to open its menu.

- The Elgato App finds the board by itself.
- In Companion, enter the address under Surfaces as a remote surface. When Companion announces its Satellite API in
  your network, the deck asks "Companion gefunden, verbinden?" instead. One key connects it over Satellite.
- The first program that connects takes the deck. Later ones are picked on the deck's home screen.
- Without a LAN cable, the deck shows a QR code for its setup WLAN after a minute.

### Factory reset

Open the menu on the deck and hold Neustart for 5 seconds, then let go. Without the deck menu, open the web UI's
login page, pick the factory reset link and confirm twice on the deck. A reset removes every setting, the API
keys, the paired hosts and an uploaded HTTPS certificate.

## Writing a server

The protocol is in [docs/protocol.md](docs/protocol.md). `tools/dbtools/server.py` is the small Python server
that the tests and benchmarks use. Start from it.

## Updates

```
cd tools
uv run python -m dbtools.dbfw pack --target p4 --version "$(git describe)" ../firmware/build/deckconnector.bin fw.dbfw
```

Upload `fw.dbfw` in the web UI, or let a server send `{"t": "ota", "url": ...}`. See [docs/ota.md](docs/ota.md).

## Tests

```
cmake -S host_test -B build-host -G Ninja && cmake --build build-host && ctest --test-dir build-host
cd tools && uv run pytest tests            # end to end, needs a board
cd tools && uv run pytest tests -m ota     # updates, restarts the board
```

The API tests also run without a board, against the real web code built for Linux (`firmware/sim`):

```
cd firmware/sim && idf.py build
cd tools && uv run pytest tests --target sim -n auto
```

Building for Linux needs `libbsd-dev`. Only tests marked `sim` run there.

The end to end tests need the board's address as `host` in `~/.config/deckbridge/bench.json`. The other keys
are in `tools/dbtools/site.py`. Tests that need a camera, a switch or a test Companion skip until those are set.

## Limits

- The ESP32-C6 has no 5 GHz radio.
- Up to four Network Dock hosts and four servers at once. Only the picked one draws and gets the input.

## License

AGPL-3.0, see [LICENSE](LICENSE).

## Third-party code

| Part | License |
|---|---|
| [ESP-IDF](https://github.com/espressif/esp-idf) and its components (esp_hosted, esp_wifi_remote, eppp_link, mdns, usb) | Apache-2.0 |
| `firmware/vendor/esp_hal_usb`, `firmware/vendor/usb`, `firmware/vendor/mdns`: patched copies from esp-idf and esp-usb, each with its LICENSE | Apache-2.0 |
| [cJSON](https://github.com/DaveGamble/cJSON) | MIT |
| [QR Code generator](https://github.com/nayuki/QR-Code-generator) (espressif/qrcode) | MIT |
| DejaVu Sans, in `db_ui/font_data.c` | Bitstream Vera license |
| Material Design Icons, in `db_ui/font_data.c` | Apache-2.0 |
| React, React DOM | MIT |
| Lucide icons (lucide-react) | ISC |
