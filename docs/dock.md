# Network Dock emulation

The board answers like an Elgato Network Dock with the Stream Deck attached to it. Bitfocus
Companion and the Stream Deck app find it over mDNS and drive the deck directly.

## Discovery

The dock is `_elg._tcp` on port 5343, with these TXT records: `dt=215`, `mf=Elgato`, `md`, `pv`,
`id` (MAC), `sn`, `fw`, `hwrev`. The serial number is `DB` and the MAC. While a deck is plugged in, the board also
announces a second service named after its serial on port 5344, with `dt=213`, `vid`, `pid`, `fw` and `prt=2`.

The board announces both services every 30 s, and again on every plug or unplug of the deck.

## Ports

| Port | Role |
|---|---|
| 5343 | The dock. Answers `0x80` identity, `0x81` to `0x83` firmware, `0x84` serial, `0x85` MAC, `0x1c` attached device, `0x8f` whether a deck is attached, and the fixed `0x87`, `0x88`, `0x8b`, `0x8e` |
| 5344 | The device. The board passes output, feature and input reports through |

Every connection gets a keepalive at once and then every 1.5 s. Hosts drop a connection after 5 s without data.
A plug or unplug of the deck sends a new `0x1c` report to every host that has asked the dock something.

The framing ("Cora") is a 16 byte header (`43 93 8a 41`, flags, HID op, message id, length) and a HID report.
`db_core/db_cora.c` builds the answers. `host_test/test_cora.c` checks them.

## The two hosts

Companion asks the dock with `GET_REPORT`. The Stream Deck app sends the same question as a plain write padded to
1024 bytes. On the device port it wants an acknowledgement for every write with the flag `0x4000` (flags `0x8200`,
4 zero bytes). Companion never sets that flag, so it tells the two apart. A feature report asked for with only the
report id gets the full 32 bytes.

Up to three hosts may hold each port at once. The setting `controller` picks the one that draws, see
[api.md](api.md).

## Tested with

- `@elgato-stream-deck/tcp` 7.8.0 (`tools/companion-check/check.mjs`): discovery, serial and firmware, brightness,
  key and strip images.
- Bitfocus Companion 5.1.2, Surfaces, Discover, Add Connection.
- Bitfocus Companion 5.2.0 nightly, as a remote surface. The deck shows up with its own serial and keeps its pages.
- Elgato Stream Deck app 7.6.1 on Windows. The dock and the Stream Deck + connect, and images and input work.
