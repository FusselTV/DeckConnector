# deckbridge

Firmware for an ESP32-P4 board that drives an Elgato Stream Deck over USB and puts it on the network. A server
renders the images and gets every key press, encoder turn and touch. The board can also act like an Elgato
Network Dock, so Bitfocus Companion and the Stream Deck app use it without any extra software.

<table><tr>
<td><img src="docs/img/status.jpg" width="380" alt=""></td>
<td><img src="docs/img/companion.jpg" width="380" alt=""></td>
</tr></table>

## What it does

- From server to the end of the USB transfer: 0.5 ms for one key, 1.7 ms for a 9 KB full screen image (LAN,
  Stream Deck +).
- LAN and WLAN at the same time. LAN wins; if a server cannot be reached over LAN, the next try goes over WLAN.
- Two servers. The first one in the list that is connected draws. If it goes away, the second takes over, at
  once when the connection closes and within 10 s when the server just goes silent.
- The newest image for each area wins, so a slow link never builds a queue.
- Signed updates for the firmware and the WLAN chip, with automatic rollback if new bridge firmware does not
  come up.
- A status screen and a settings menu on the device itself, a web UI, and a setup WLAN for first use.

## Hardware

- Waveshare ESP32-P4-WIFI6-POE-ETH (ESP32-P4 with an ESP32-C6 for WLAN, PoE, USB-A on the high speed PHY).
- Stream Deck + (tested). Stream Deck MK.2 and XL are in the driver table but not tested yet.

The board powers the deck from its USB-A port. With PoE the whole setup runs from one cable.

## Build and flash

Needs ESP-IDF 6.0.2 and [uv](https://docs.astral.sh/uv/).

```
git clone git@github.com:FusselTV/deckbridge.git
```

```
cd deckbridge/firmware
idf.py build
idf.py -p /dev/ttyACM0 flash
```

The first start creates a password for the web UI. The device shows it under Menu, Info.

## Setup

1. Plug in LAN, or wait a minute. Without a network the board opens a setup WLAN. Its name and password are on
   the device.
2. Open `http://<address>/` and log in as `admin`.
3. Choose the mode.
   - **Native**: enter one or two server URLs (`ws://host:port/deckbridge`). Leave them empty to find servers that
     announce `_deckbridge._tcp` over mDNS.
   - **Network Dock**: the board shows up in Companion under Surfaces, Discover. Click Add Connection.

The menu on the device opens when you hold the first and the last encoder for two seconds.

## Writing a server

The protocol is in [docs/protocol.md](docs/protocol.md). `tools/dbtools/server.py` is a small Python server that the
tests and benchmarks use, and a good starting point.

## Updates

```
cd tools
uv run python -m dbtools.dbfw pack --target p4 --version "$(git describe)" ../firmware/build/deckbridge.bin fw.dbfw
```

Upload `fw.dbfw` in the web UI, or let a server send `{"t": "ota", "url": ...}`. See [docs/ota.md](docs/ota.md).

## Tests

```
cmake -S host_test -B build-host -G Ninja && cmake --build build-host && ctest --test-dir build-host
cd tools && uv run pytest tests            # end to end, needs the bench
cd tools && uv run pytest tests -m ota     # updates, restarts the board
```

## Limits

- `ws://` only, no TLS.
- The ESP32-C6 has no 5 GHz radio.
- In Network Dock mode the board looks like a dock to the host. The Stream Deck app is not tested yet.
- In Network Dock mode the host is not told when the menu drew over its picture; it shows again on its next update.
