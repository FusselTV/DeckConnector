# Deckbridge protocol, version 1

The bridge connects to one or two servers over WebSocket. The server renders images and handles input. The
bridge knows the USB protocol of the device and shows the newest image for each area.

## Connection

- The bridge is the client. It opens `ws://<server>/deckbridge` with the subprotocol `deckbridge.v1`.
- The bridge keeps a connection to every configured server, at most two. The first server in the list that is
  connected is **active**. The other one is **standby**.
- Only the active server may send images. Images from a standby server are answered with `REJECTED_ROLE`.
- Input goes to the active server only.
- When the active server is lost, the standby server becomes active and gets a `role` message. It must then
  draw every area again, because the bridge does not cache old images.
- WebSocket ping frames are sent every 5 s. A server that does not answer for 15 s counts as lost.

Text frames carry JSON control messages. Binary frames carry images, acknowledgements, input and time sync.
All binary integers are little endian.

## Coordinates

Every display has one coordinate system with the origin at the top left of the whole glass, in pixels. Keys and
touch strips are rectangles inside it. The server draws in these coordinates and sends any rectangle that the
device can address (see `targets` in the `device` message). The bridge picks the matching USB command.

Rotation and flipping are listed per display. The server applies them before it encodes the JPEG.

## Binary messages, server to bridge

| Type | Name | Layout after the type byte |
|---|---|---|
| `0x01` | IMAGE | `u8 display, u16 x, u16 y, u16 w, u16 h, u32 seq`, then the JPEG data |
| `0x02` | TIME | `u8[7] reserved, u64 server_time_us` |

`seq` is chosen by the server and comes back in the acknowledgement. A newer image that covers an older pending
image replaces it. The older one is acknowledged as `SUPERSEDED`.

## Binary messages, bridge to server

| Type | Name | Layout after the type byte |
|---|---|---|
| `0x81` | ACK | `u8 result, u8 display, u8 reserved, u32 seq, u64 t_recv, u64 t_start, u64 t_done` |
| `0x82` | TIME | `u8[7] reserved, u64 server_time_us, u64 bridge_time_us` |
| `0x83` | INPUT | `u8 kind, u8 index, u8 display, i16 value, u16 x, u16 y, u16 x2, u16 y2, u8[3] reserved, u64 t` |

Times are microseconds on the bridge clock. `t_recv` is when the image was complete, `t_start` and `t_done`
frame the USB transfer. INPUT `t` is when the USB report arrived.

ACK `result`:

| Value | Meaning |
|---|---|
| 0 | `SHOWN`, sent to the device |
| 1 | `SUPERSEDED`, replaced by a newer image before it was sent |
| 2 | `REJECTED_RECT`, the rectangle is not a target of this display |
| 3 | `REJECTED_ROLE`, this server is not active, or the menu or dock mode owns the device |
| 4 | `NO_DEVICE`, no device is connected |
| 5 | `BUSY`, the bridge is out of image memory |

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

Send the next image for an area only after the ACK of the previous one for that area. The bridge also drops
covered images, so a fast server never builds a queue, but waiting for the ACK keeps the latency low on slow
links like WLAN.

## JSON messages, bridge to server

```json
{"t": "hello", "proto": 1, "id": "80f1b2a1b2c3", "name": "deckbridge", "fw": "0.1.0",
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
{"t": "status", "uptime": 3600, "heap": 123456, "psram": 30000000, "lan": "192.168.1.214",
 "wlan": "192.168.2.237", "rssi": -52, "usb_errors": 0, "images": 1234, "superseded": 12}
{"t": "ota", "target": "p4", "state": "download", "pct": 40}
{"t": "ota", "target": "p4", "state": "error", "error": "signature"}
```

OTA `state` is one of `download`, `verify`, `done`, `error`. After `done` the bridge restarts.

## JSON messages, server to bridge

```json
{"t": "brightness", "pct": 70}
{"t": "ota", "target": "p4", "url": "http://server:8000/deckbridge-0.2.0.dbfw"}
{"t": "reboot"}
```

`target` is `p4` for the bridge firmware or `c6` for the WLAN chip. The file must be a signed `.dbfw` image, see
[ota.md](ota.md).
