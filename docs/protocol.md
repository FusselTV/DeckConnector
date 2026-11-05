# Deck-Connector protocol, version 1

The Deck-Connector connects to one or two servers over WebSocket. The server renders images and handles input. The
bridge knows the USB protocol of the device and shows the newest image for each area.

## Connection

- The Deck-Connector is the client. It opens `ws://<server>/deckbridge` with the subprotocol `deckbridge.v1`.
  A `wss://` URL works too. The server's certificate must come from a public CA and name the host, or match the
  SHA-256 set for that server in `server_pins` ([api.md](api.md)).
- The Deck-Connector keeps a connection to every configured server, at most two. The first server in the list that is
  connected is **active**. The other one is **standby**.
- Only the active server may send images. The Deck-Connector answers images from a standby server with `REJECTED_ROLE`.
- Input goes to the active server only.
- When the active server is lost, the standby server becomes active and gets a `role` message. It must then
  draw every area again. The Deck-Connector first shows what this server drew last time, which may be old. The active
  server gets `role` again when the menu or an update on the device drew over its picture.
- While the controller setting is `app` or `companion`, every server is standby. See [api.md](api.md).
- The Deck-Connector sends a ping every 2 s. A server that sends nothing for 6 s counts as lost.

Text frames carry JSON control messages. Binary frames carry images, acknowledgements, input and time sync.
All binary integers are little endian.

## Coordinates

Every display has one coordinate system with the origin at the top left of the whole glass, in pixels. Keys and
touch strips are rectangles inside it. The server draws in these coordinates and sends any rectangle that the
device can address (see `targets` in the `device` message). The Deck-Connector picks the matching USB command.

Rotation and flipping are listed per display. The server applies them before it encodes the JPEG.

## Binary messages, server to Deck-Connector

| Type | Name | Layout after the type byte |
|---|---|---|
| `0x01` | IMAGE | `u8 display, u16 x, u16 y, u16 w, u16 h, u32 seq`, then the JPEG data |
| `0x02` | TIME | `u8[7] reserved, u64 server_time_us` |

`seq` is chosen by the server and comes back in the acknowledgement. A newer image that covers an older pending
image replaces it. The older one is acknowledged as `SUPERSEDED`.

## Binary messages, Deck-Connector to server

| Type | Name | Layout after the type byte |
|---|---|---|
| `0x81` | ACK | `u8 result, u8 display, u8 reserved, u32 seq, u64 t_recv, u64 t_start, u64 t_done` |
| `0x82` | TIME | `u8[7] reserved, u64 server_time_us, u64 bridge_time_us` |
| `0x83` | INPUT | `u8 kind, u8 index, u8 display, i16 value, u16 x, u16 y, u16 x2, u16 y2, u8[10] reserved, u64 t` |

Times are microseconds on the Deck-Connector clock. `t_recv` is when the image was complete, `t_start` and `t_done`
frame the USB transfer. INPUT `t` is when the USB report arrived.

ACK `result`:

| Value | Meaning |
|---|---|
| 0 | `SHOWN`, sent to the device |
| 1 | `SUPERSEDED`, replaced by a newer image before it was sent |
| 2 | `REJECTED_RECT`, the rectangle is not a target of this display |
| 3 | `REJECTED_ROLE`, this server is not active, or the menu, the Stream Deck app or Companion owns the device |
| 4 | `NO_DEVICE`, no device is connected, or the USB transfer failed |
| 5 | `BUSY`, the Deck-Connector is out of image memory |

INPUT `kind`:

| Value | Kind | Fields used |
|---|---|---|
| 1 | `KEY_DOWN` | `index` |
| 2 | `KEY_UP` | `index` |
| 3 | `ENCODER_DOWN` | `index` |
| 4 | `ENCODER_UP` | `index` |
| 5 | `ENCODER_TURN` | `index`, `value` (ticks, positive is clockwise) |
| 6 | `TOUCH_TAP` | `display`, `x`, `y` |
| 7 | `TOUCH_LONG` | `display`, `x`, `y` |
| 8 | `TOUCH_SWIPE` | `display`, `x`, `y` to `x2`, `y2` |

Touch positions are in display coordinates.

## Flow control

Send the next image for an area only after the ACK of the previous one for that area. The Deck-Connector also drops
covered images, so a fast server never builds a queue, but waiting for the ACK keeps the latency low on slow
links like WLAN. An ACK can get lost when the link is overloaded; treat an area as free again after 2 s without
an ACK.

## JSON messages, Deck-Connector to server

```json
{"t": "hello", "proto": 1, "id": "80f1b2a1b2c3", "name": "deck-connector-a1b2c3", "fw": "0.1.0",
 "c6": "3.0.10", "board": "waveshare-p4-poe-eth", "active": true}
```

```json
{"t": "device", "present": true, "vendor": "Elgato", "model": "Stream Deck +", "vid": 4057, "pid": 132,
 "serial": "A00SA0000AA0A0", "fw": "1.01.008", "tested": true, "keys": 8, "encoders": 4,
 "displays": [{"id": 0, "w": 800, "h": 480, "format": "jpeg", "rotate": 0, "flip_x": false, "flip_y": false,
   "targets": [{"kind": "full", "x": 0, "y": 0, "w": 800, "h": 480},
               {"kind": "key", "index": 0, "x": 13, "y": 12, "w": 120, "h": 120},
               {"kind": "strip", "x": 0, "y": 380, "w": 800, "h": 100, "partial": true}]}],
 "touch": [{"display": 0, "x": 0, "y": 380, "w": 800, "h": 100}]}
```

A `strip` target with `partial: true` accepts any rectangle inside it. Other targets accept only their exact
rectangle. `{"t": "device", "present": false}` means the device was unplugged.

```json
{"t": "role", "active": true}
{"t": "status", "name": "deck-connector-a1b2c3", "firmware": "v0.1.0", "controller": "server", "writer": "server1", ...}
{"t": "ota", "target": "p4", "state": "download", "pct": 40}
{"t": "ota", "target": "p4", "state": "error", "error": "signature"}
```

`status` has the fields of `GET /api/status` ([api.md](api.md)) without `memory`.

OTA `state` is one of `download`, `verify`, `done`, `error`. After `done` the Deck-Connector restarts.

## JSON messages, server to Deck-Connector

```json
{"t": "brightness", "pct": 70}
{"t": "ota", "target": "p4", "url": "http://server:8000/deckbridge-0.2.0.dbfw"}
{"t": "reboot"}
```

The signed `.dbfw` file decides whether it goes to the Deck-Connector (P4) or to the WLAN chip (C6); `target` is only
echoed in the progress messages. See [ota.md](ota.md).
