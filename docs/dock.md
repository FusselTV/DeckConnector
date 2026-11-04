# Network Dock emulation

In dock mode the board answers like an Elgato Network Dock with the Stream Deck attached to it.
Hosts that support the dock, such as Bitfocus Companion, find it over mDNS and drive the deck directly.

## Discovery

`_elg._tcp` on port 5343, with these TXT records: `dt=215`, `mf=Elgato`, `md`, `pv`, `id` (MAC),
`sn`, `fw`, `hwrev`. The serial number is `DB` and the MAC.

## Ports

| Port | Role |
|---|---|
| 5343 | The dock. Answers `0x80` identity, `0x83` firmware, `0x84` serial, `0x85` MAC, `0x1c` attached device. |
| 5344 | The device. Output reports, feature reports and input reports are passed through unchanged. |

Both ports send the dock keepalive every 1.5 s; hosts drop the connection after 5 s without data. A plug or
unplug of the deck sends a new `0x1c` report on the dock port.

The framing ("Cora") is a 16 byte header (`43 93 8a 41`, flags, HID op, message id, length) and a HID report.
`db_core/db_cora.c` builds the answers; `host_test/test_cora.c` checks them.

## Tested with

- `@elgato-stream-deck/tcp` 7.8.0 (`tools/companion-check/check.mjs`): discovery, serial and firmware, brightness,
  key and strip images.
- Bitfocus Companion 5.1.2 (Docker): Surfaces, Discover lists the board; Add Connection opens the Stream Deck +.
