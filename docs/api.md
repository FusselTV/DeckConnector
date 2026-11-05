# Web API

Everything the web UI does goes through this JSON API on port 80. Scripts can use it the same way.

## Login

Send the password as HTTP basic auth with the user `admin`. Every request that changes something also needs the
header `X-Deckbridge: 1`.

```
curl -u admin:PASSWORD http://192.168.1.214/api/status
curl -u admin:PASSWORD -H 'X-Deckbridge: 1' -X PUT -d '{"controller":"app"}' http://192.168.1.214/api/config
```

The web UI logs in with `POST /api/login` and `{"password": "..."}` and gets a session cookie for 12 hours.
`POST /api/logout` ends it. After five wrong passwords the login locks for a few seconds, up to a minute.

Errors are JSON with a matching HTTP status: `{"ok": false, "error": "login required"}`.

## Endpoints

| Method and path | What it does |
|---|---|
| `GET /api/status` | State of the bridge, see below |
| `GET /api/config` | Settings. Passwords and the token only show as `wifi_pass_set` and `token_set` |
| `PUT /api/config` | Change settings. Send only the keys you change |
| `GET /api/screen` | What the deck shows right now, see below |
| `GET /api/screen/<id>.jpg` | One image of that picture |
| `POST /api/input` | Press a key, turn an encoder or touch the strip, see below |
| `POST /api/menu` | Open the menu on the deck. 409 without a deck |
| `GET /api/log` | The last 16 KB of the log, as text |
| `POST /api/ota` | Upload a signed `.dbfw` file as the body. The bridge restarts when it is done |
| `POST /api/reboot` | Restart |
| `GET /api/profile` | Time spent per endpoint and CPU share per task since the previous call |

## Who controls the deck

`controller` in the settings picks who draws on the deck and gets the key presses.

| Value | Who |
|---|---|
| `server` | Your own servers over WebSocket ([protocol.md](protocol.md)) |
| `app` | The Elgato Stream Deck app, through the Network Dock emulation |
| `companion` | Bitfocus Companion, through the Network Dock emulation |

The change works at once. All of them stay connected, and the deck shows the picked one's picture right away.
The others keep drawing into their own picture in the background and get no input. The same choice is in the menu
on the device, key 4.

`brightness` also works at once. Every other change restarts the bridge, and the reply says so:
`{"ok": true, "restart": true}`.

## Settings

```json
{"name": "deckbridge-a1b2c3", "controller": "companion", "brightness": 80,
 "lan": {"dhcp": true, "ip": "", "mask": "", "gateway": "", "dns": ""},
 "wlan": {"dhcp": true, "ip": "", "mask": "", "gateway": "", "dns": ""},
 "wifi_ssid": "deckbridge", "wifi_pass_set": true,
 "servers": ["ws://192.168.1.128:8765/deckbridge", "ws://192.168.1.128:8766/deckbridge"], "token_set": false}
```

To change them, `PUT` the same keys. The secret ones are `wifi_pass`, `token` and `admin_pass`. `admin_pass`
needs at least 6 characters. An empty `servers` list finds servers over mDNS. A fixed address needs `ip` and `mask`.

## Status

```json
{"name": "deckbridge-a1b2c3", "firmware": "v0.1.0", "wlan_firmware": "3.0.10", "uptime_s": 23,
 "controller": "companion", "writer": "companion",
 "device": {"model": "Stream Deck +", "serial": "EL00K1A00001", "firmware": "2.0.3.7", "keys": 8, "encoders": 4,
            "width": 800, "height": 480},
 "network": {"lan": {"cable": true, "ip": "192.168.1.214"},
             "wlan": {"ssid": "deckbridge", "ip": "192.168.1.237", "rssi": -23}},
 "servers": [{"url": "ws://192.168.1.128:8765/deckbridge", "connected": false, "active": false}],
 "dock_hosts": [{"kind": "companion", "ip": "192.168.1.15", "device": true}],
 "counters": {"images": 7, "superseded": 0, "rejected": 0, "usb_errors": 0, "inputs": 0, "inputs_dropped": 0},
 "memory": {"heap_free": 353203, "heap_min_free": 290191, "psram_free": 32211772, "stack_free": {"ui": 3688}}}
```

- `writer` is who draws at this moment: `ui`, `server1`, `server2`, `app` or `companion`. It is `ui` while the
  menu is open, an update runs, or the picked controller is not connected.
- `device` is `null` without a Stream Deck.
- `dock_hosts` lists the Network Dock hosts. `kind` is `app`, `companion`, or `unknown` until the host asks
  something. `device` is true when the host also uses the deck.
- `superseded` counts images that a newer one replaced before they were drawn.

## What the deck shows

```json
{"writer": "companion", "width": 800, "height": 480,
 "keys": [{"x": 13, "y": 12, "w": 120, "h": 120}, ...], "touch": [{"x": 0, "y": 380, "w": 800, "h": 100}],
 "encoders": 4, "menu_chord": {"on": "encoders", "first": 0, "last": 3},
 "images": [{"x": 13, "y": 12, "w": 120, "h": 120, "url": "/api/screen/9.jpg"},
            {"x": 232, "y": 12, "w": 120, "h": 120, "url": "/api/screen/10.jpg"}]}
```

`keys` lists the keys in index order, `touch` the touch strips. `menu_chord` names the two encoders or keys
(`on` is `encoders` or `keys`) that open the menu when held together for 2 seconds. `last` below 1 means
there is no such pair.

Draw the JPEGs in this order at their place to get the picture. On the Stream Deck + the keys are at their place
on the 800 x 480 screen, and the touch strip is the bottom 100 pixels. An image keeps its URL until something
covers it, so a browser may cache it. A URL that is gone answers 404. Without a Stream Deck, `width` and `height`
are missing and `images` is empty.

## Input

`POST /api/input` acts as if you used the deck. Whoever draws gets it exactly like a real press. Keys and encoders
count from 0. Coordinates are those of `/api/screen`.

```json
{"key": 3}
{"key": 3, "action": "down"}
{"key": 3, "action": "up"}
{"encoder": 0, "action": "turn", "ticks": -2}
{"encoder": 0}
{"touch": "tap", "x": 100, "y": 430}
{"touch": "long", "x": 100, "y": 430}
{"touch": "swipe", "x": 700, "y": 430, "x2": 100, "y2": 430}
```

`action` is `press` (down and up, the default), `down`, `up`, or `turn` for encoders. Positive ticks turn right.
The menu opens with `down` on the two controls in `menu_chord` and `up` after 2 s. A press on the real deck in between ends
the simulated hold, because the deck always reports all keys at once.
