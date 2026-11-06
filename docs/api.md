# Web API

Everything the web UI does goes through this JSON API on port 80, and the same over HTTPS on port 443. Scripts
can use it the same way. The Deck-Connector announces it over mDNS as `_deckbridge-api._tcp`, with `path` and `fw` in the
TXT record.

The Deck-Connector makes its own HTTPS certificate on the first start and keeps it. Browsers warn about it once.
`https_fingerprint` in the status is its SHA-256, to compare with what the browser shows. With `https_only` set,
plain HTTP answers the page with a redirect to HTTPS and everything else with 403, so no password goes over the
network unencrypted. The setup access point stays on plain HTTP.

## Login

Send the password as HTTP basic auth with the user `admin`. Every request that changes something also needs the
header `X-Deckbridge: 1`.

```
curl -u admin:PASSWORD http://192.168.1.214/api/status
curl -u admin:PASSWORD -H 'X-Deckbridge: 1' -X PUT -d '{"controller":"app","host":""}' http://192.168.1.214/api/config
```

The web UI logs in with `POST /api/login`, the header `X-Deckbridge: 1` and `{"password": "..."}`, and gets a
session cookie for 12 hours. `POST /api/logout` ends it. After five wrong passwords from one address, that address
waits a few seconds, up to a minute, for both kinds of login.

## For integrations

Home Assistant and other programs use a token instead of the password. Make one in the web UI under System, or
with the password:

```
curl -u admin:PASSWORD -H 'X-Deckbridge: 1' -d '{"name":"home-assistant"}' http://192.168.1.214/api/tokens
curl -H 'Authorization: Bearer TOKEN' http://192.168.1.214/api/status
```

The reply shows the token once. The Deck-Connector keeps only its SHA-256, and a settings backup never holds it. A
token needs no `X-Deckbridge` header. It reaches the routes that `GET /api` lists with `"access": "control"`:
status, screen, metrics, input, menu, reconnect and the web socket without changing settings. Everything else
answers 403 to a token. `DELETE /api/tokens/<name>` ends a token at once. A new token with the same name replaces
the old one. Up to 8 tokens exist at a time.

A page from another origin may call the API with a token when its origin is in `cors_origins` (comma separated).
It is empty, so off, by default. Cookies never work across origins.

Errors are JSON with a matching HTTP status: `{"ok": false, "error": "login required"}`.

## Endpoints

| Method and path | What it does |
|---|---|
| `GET /` | The web UI, gzip only |
| `GET /api` | Every route with `method`, `path` and `access` (`open`, `control` for tokens, `admin` for the password) |
| `OPTIONS /api/...` | CORS preflight, see "For integrations" |
| `GET /api/status` | State of the Deck-Connector, see below |
| `GET /api/config` | Settings. Passwords and the token only show as `wifi_pass_set` and `token_set` |
| `PUT /api/config` | Change settings. Send only the keys you change |
| `GET /api/screen` | What the deck shows right now, see below |
| `GET /api/screen/<id>.jpg` | One image of that picture |
| `GET /api/screen.jpg` | The whole picture as one JPEG, for a camera in Home Assistant |
| `POST /api/input` | Press a key, turn an encoder or touch the strip, see below |
| `POST /api/menu` | Open the menu on the deck. 409 without a deck |
| `POST /api/dock` | Pairing of Companion and Stream Deck app hosts, see below |
| `POST /api/deck/reconnect` | Switch the deck's USB port off and on, as if the cable were pulled and plugged in |
| `GET /api/log` | The last 16 KB of the log, as text |
| `POST /api/ota` | Upload a signed `.dbfw` file as the body. It goes into the slot after the running one, and the Deck-Connector restarts into it. `?slot=N` writes into slot N instead, `?boot=0` keeps the running firmware. The running slot is never written |
| `GET /api/slots` | The firmware slots: `slot`, `label`, `state` (`valid`, `pending` until a new start confirms itself, `invalid`, `empty`, `unknown` for an image the bootloader does not track, such as one written before a USB flash), `version`, `built`, `running`, `next` (starts at the next restart), `level` (security level, missing when not known) |
| `POST /api/slots/N/boot` | Restart into slot N. Refused (409) for an `invalid` slot, a slot without a known level or one below the security level, and while the running firmware has not confirmed itself yet |
| `POST /api/ota/url` | Send `{"url": "http://..."}` and the Deck-Connector loads a signed `.dbfw` file itself. Progress comes over `/api/ws` |
| `POST /api/reboot` | Restart |
| `GET /api/metrics` | CPU, RAM and PSRAM in percent every 5 s for the last 10 minutes, oldest first, and how full the flash is. Kept in RAM only |
| `GET /api/profile` | Time spent per endpoint and CPU share per task since the previous call |
| `GET /api/tokens` | The names of the API tokens |
| `POST /api/tokens` | `{"name": "..."}` makes a token and shows it once |
| `DELETE /api/tokens/<name>` | Ends that token |
| `GET /api/ws` | Web socket with live changes, see below |

The routes that act on a deck (screen, input, menu, reconnect) take `?deck=<serial>` from `decks` in the status.
Without it they act on the first deck. A deck that is not attached gets a 404.

## Who controls the deck

Every host that can draw is in one list: Companion over its Satellite API ([satellite.md](satellite.md)), hosts
that use the Network Dock emulation (Companion, the Stream Deck app), and your own servers
([protocol.md](protocol.md)). Up to 4 servers and up to 4 dock hosts can be connected at once, in any mix.
`sources` in the status is that list.

Two settings pick one entry of it, and they go together. `host` names the entry: a dock host's IP, a server's URL,
or `satellite`. `controller` is the program behind it.

| `controller` | Program |
|---|---|
| `companion` | Bitfocus Companion, over Satellite or as a dock host |
| `app` | The Elgato Stream Deck app, as a dock host |
| `server` | Your own servers |

To pick an entry, send both: `{"controller": "companion", "host": "satellite"}`. When the picked host is not
connected, the first connected one of the same program draws, for `companion` the Satellite connection first.
`controller` alone with an empty `host` means exactly that from the start, so settings from older firmware keep
working.

The change works at once. All hosts stay connected, and the deck shows the picked one's picture right away. The
others keep drawing into their own picture in the background and get no input. On the device the hosts are the top
row of the home screen and the menu.

`brightness`, `controller`, `host`, `servers`, `server_pins`, `satellite`, `satellite_pin` and `https_only` also
work at once. Every other change restarts the Deck-Connector, and the reply says so:
`{"ok": true, "restart": true}`.

## Settings

```json
{"name": "deck-connector-a1b2c3", "controller": "companion", "host": "192.168.1.15", "brightness": 80,
 "lan": {"dhcp": true, "ip": "", "mask": "", "gateway": "", "dns": ""},
 "wlan": {"dhcp": true, "ip": "", "mask": "", "gateway": "", "dns": ""},
 "wifi_ssid": "deckbridge", "wifi_pass_set": true,
 "servers": ["ws://192.168.1.128:8765/deckbridge", "wss://example.org/deckbridge", "", ""],
 "server_pins": ["", "", "", ""], "satellite": "192.168.1.15", "satellite_pin": "",
 "https_only": false, "cors_origins": "", "token_set": false}
```

To change them, `PUT` the same keys. The secret ones are `wifi_pass`, `token` and `admin_pass`. `admin_pass`
needs at least 6 characters. `servers` holds up to 4 URLs. An empty list finds a server over mDNS. A fixed
address needs `ip` and `mask`. `satellite` is the address of Companion for its Satellite API, `host` or
`host:port`, port 16622 when left out. A `ws://` or `wss://` URL goes to Companion's WebSocket port 16623, or to
a proxy in front of it. Empty turns it off.

A `wss://` address must show a certificate from one of the 41 most used public CAs, made out to its host name.
For any other certificate, put its SHA-256 into `server_pins` (same position as the URL in `servers`) or
`satellite_pin`. Then only that certificate counts, and the name in it does not matter. `openssl x509 -noout
-fingerprint -sha256` prints it, colons are fine. The Deck-Connector never talks TLS to a host it cannot check.

The secret keys never come out of `GET /api/config`. So its reply is a backup without passwords, and a `PUT` of
it restores the settings. The web UI does this under System.

## Status

```json
{"id": "a0b1c2a1b2c3", "name": "deck-connector-a1b2c3", "firmware": "v0.1.0", "wlan_firmware": "3.0.10", "uptime_s": 23,
 "controller": "companion", "writer": "dock2",
 "device": {"model": "Stream Deck +", "serial": "EL00K1A00001", "firmware": "2.0.3.7", "keys": 8, "encoders": 4,
            "width": 800, "height": 480},
 "network": {"lan": {"cable": true, "ip": "192.168.1.214"},
             "wlan": {"ssid": "deckbridge", "ip": "192.168.1.237", "rssi": -23}},
 "sources": [{"kind": "server", "host": "ws://192.168.1.128:8765/deckbridge", "name": "", "connected": false,
              "chosen": false, "drawing": false},
             {"kind": "companion", "host": "192.168.1.15", "name": "studio", "connected": true, "chosen": true,
              "drawing": true}],
 "counters": {"images": 7, "superseded": 0, "rejected": 0, "usb_errors": 0, "inputs": 0, "inputs_dropped": 0},
 "boot_ms": {"deck": 537, "lan_link": 2688, "wlan_ip": 5724, "lan_ip": 6692, "companion": 7439, "host_draws": 7665},
 "memory": {"heap_free": 353203, "heap_min_free": 290191, "psram_free": 32211772, "heap_largest": 208896,
            "psram_largest": 30932992, "tasks": 37, "sockets": 9, "sockets_max": 32, "stack_free": {"ui": 3688}}}
```

- `writer` is who draws at this moment: `ui`, `server1` to `server4`, `dock1` to `dock4`, or `satellite`. The
  source with `drawing` in `sources` says which host that is. It is `ui` while the menu is open, an update runs,
  or the picked host is not connected.
- `boot_ms` says when each step of this boot happened first, in ms after the firmware started: the deck, the
  LAN link, each address, Companion, the Stream Deck app, a server, the first picture of a host, and Companion
  over its Satellite API.
- `heap_largest`, `psram_largest`, `tasks` and `sockets` show fragmentation and leaks over a long run.
- `id` is the Deck-Connector's MAC address. It stays when the name changes.
- `device` is `null` without a Stream Deck. `decks` lists every attached deck with the same fields.
- `sources` lists every server that is set or was found, Companion's Satellite API when an address is set (with
  `"via": "satellite"`), and every Network Dock host. A dock host's `kind` is
  `unknown` until it asks something, and it is `connected` once it also uses the deck. `name` is what the router
  calls the host, empty when it has no name or is a server. `chosen` marks the one that draws whenever no menu
  covers it.
- `superseded` counts images that a newer one replaced before they were drawn.
- `usb_errors` counts failed USB transfers. Transfers that an unplug ends are not counted.

## What the deck shows

```json
{"writer": "dock2", "width": 800, "height": 480,
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

`POST /api/input` acts as if you used the deck. Keys and encoders count from 0. Coordinates are those of
`/api/screen`.

Companion, the Stream Deck app and the Satellite connection get the input only with `"real": true`, because their
buttons switch real devices. Send it only for a person who pressed something, as the web UI does. Without it, only
the Deck-Connector's own screens and your own servers get the input. `simulated_input: "local"` in the status says
the firmware works this way.

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
The menu opens with `down` on the two controls in `menu_chord` and `up` after 2 s. A press on the real deck in
between ends the simulated hold, because the deck always reports all keys at once.

## Pairing

Companion and the Stream Deck app connect without a password. The Deck-Connector lists every host that connected, by its
MAC address, so a laptop that gets a new address from DHCP stays the same host. With `dock_paired_only` set in
the settings, only listed hosts get in. A new host is then let in and listed while none is known, in the first 5
minutes after a boot, and while pairing is open. Otherwise it is turned away, and the deck and the web UI ask
whether to let it in. Up to 8 hosts are listed.

`pairing` in the status has `only_paired`, the waiting `request` or `null`, `open` while new hosts get in,
`open_s` how long still, and `hosts` with `ip`, `mac`, `name`, `kind` and `connected`. `name`, and
`request_name` for the waiting host, is what the router's DNS answers for the address, empty when it does not
know one. `POST /api/dock` takes one of
these:

```json
{"accept": true}
{"accept": false}
{"pairing": 300}
{"pairing": 0}
{"forget": "192.168.1.15"}
{"forget": true}
```

## Live changes

`/api/ws` is a web socket with the same login as the other endpoints. A browser page from another site is
refused. Every message is one JSON object, and `t` says what it is.

| `t` | When |
|---|---|
| `state` | At once on connect, then on every change. The status without uptime, signal and counters |
| `screen` | The picture changed. `GET /api/screen` or `/api/screen.jpg` has the new one |
| `input` | Someone used the deck: `kind`, `index`, `value`, `x`, `y`, `x2`, `y2` and the `writer` that got it |
| `ota` | Progress of an update: `target`, `state`, `pct`, and `error` when it failed |
| `ping` | Every 5 s. Without any message for 12 s the connection is dead, so connect again |

To change something, send `{"t": "set", "id": 1, ...}` with the keys of `PUT /api/config`, or
`{"t": "input", "id": 2, ...}` with the keys of `POST /api/input`. The answer is
`{"t": "result", "id": 1, "status": 200, "reply": {...}}`.
