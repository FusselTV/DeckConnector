# Web API

Everything the web UI does goes through this JSON API on port 80, and the same over HTTPS on port 443. Scripts
can use it the same way. The DeckConnector announces it over mDNS as `_deckconnector-api._tcp` (and the old `_deckbridge-api._tcp`), with `path` and
`fw` in the TXT record.

The DeckConnector makes its own HTTPS certificate on the first start and keeps it. Browsers warn about it once.
`https_fingerprint` in the status is its SHA-256, to compare with what the browser shows. With `https_only` set,
plain HTTP answers the page with a redirect to HTTPS and everything else with 403, so no password goes over the
network unencrypted. The setup access point stays on plain HTTP.

## Login

Send the password as HTTP basic auth with the user `admin`. Every request that changes something also needs the
header `X-DeckConnector: 1`. The old `X-Deckbridge: 1` works too.

```
curl -u admin:PASSWORD http://192.168.1.214/api/status
curl -u admin:PASSWORD -H 'X-DeckConnector: 1' -X PUT -d '{"controller":"app","host":""}' http://192.168.1.214/api/config
```

The web UI logs in with `POST /api/login`, the header `X-DeckConnector: 1` and `{"password": "..."}`, and gets a
session cookie. The session ends after 12 hours without use, or with `POST /api/logout`. It stays over a restart of
the connector. A power loss or a new password ends all sessions. After five wrong passwords from one address, that
address waits a few seconds, up to a minute, for both kinds of login.

Two routes help someone who lost the password. Both need no login, only the header `X-DeckConnector: 1`. They
answer 409 when no deck is attached. A second call within 3 seconds changes nothing.

| Method and path | What it does |
|---|---|
| `POST /api/password/show` | The deck shows the user and the password for 30 seconds. Any key on the deck hides it earlier. Answers `{"ok": true, "decks": 1}` |
| `POST /api/password/reset` | The deck asks for 60 seconds whether to reset all settings. Two taps on "Zurücksetzen" on the deck itself reset it and restart it. Presses sent over the API do not count. Answers `{"ok": true, "s": 60}` with the seconds left |
| `DELETE /api/password/reset` | Ends the question. Answers `{"ok": true}` |

While the deck shows the password, a token gets 403 for the pictures of the deck.

A browser can also ask to log in without the password. The deck shows its address, the label it sent, its MAC
address when it is on the same network, and its name when the router knows one. Someone at the deck taps
"Annehmen" or "Ablehnen". Presses sent over the API do not count. The question ends after 60 seconds. There is one
question at a time, and a new one replaces the older one.

| Method and path | What it does |
|---|---|
| `POST /api/login/request` | Needs the header `X-DeckConnector: 1`. Body `{"who": "iPhone · Safari"}`, up to 48 characters. Answers `{"id": "<32 hex digits>", "s": 60}`. 409 when no deck is attached, 429 within 3 seconds of the last request |
| `GET /api/login/request/<id>` | Answers `{"state": "pending", "s": 42}`. `state` is `pending`, `accepted`, `denied`, `expired` or `used`. With `accepted` the reply sets the session cookie, like `POST /api/login`. This happens once, then the state is `used`. 404 for an unknown id |
| `DELETE /api/login/request/<id>` | Needs the header `X-DeckConnector: 1`. Takes the request back. Answers `{"ok": true}` |

A client on the setup access point needs no password. To join that WLAN you need its password, and only the deck
shows it. Every request over it has the rights of a password login. Changes still need the header
`X-DeckConnector: 1`. `via_ap` in `GET /api/status` is true for such a request.

## For integrations

Home Assistant and other programs use a token instead of the password. Make one in the web UI under System, or
with the password:

```
curl -u admin:PASSWORD -H 'X-DeckConnector: 1' -d '{"name":"home-assistant","right":"full"}' http://192.168.1.214/api/tokens
curl -H 'Authorization: Bearer TOKEN' http://192.168.1.214/api/status
```

The reply shows the token once. The DeckConnector keeps only its SHA-256, and a settings backup never holds it. A
token needs no `X-DeckConnector` header. Each token has one right:

| `right` | Reaches |
|---|---|
| `control` (default) | The routes `GET /api` lists with `"access": "control"`: status, screen, metrics, input, menu, reconnect, and the web socket without changing settings |
| `full` | Also the `"access": "full"` routes: settings, restart, updates, slots, log, pairing |

No token reaches the `"access": "admin"` routes (the tokens themselves and the crash dump), and no token changes `admin_pass`, `setup_done` or `qr_login`. Those
answer 403. `DELETE /api/tokens/<name>` ends a token at once. A new token with the same name replaces
the old one. Up to 8 tokens exist at a time.

A page from another origin may call the API with a token when its origin is in `cors_origins` (comma separated).
It is empty, so off, by default. Cookies never work across origins.

Errors are JSON with a matching HTTP status: `{"ok": false, "error": "login required"}`.

## Endpoints

| Method and path | What it does |
|---|---|
| `GET /` | The web UI, gzip only |
| `GET /api` | Every route with `method`, `path` and `access` (`open`, `control`, `full`, or `admin` for the password only) |
| `OPTIONS /api/...` | CORS preflight, see "For integrations" |
| `GET /api/status` | State of the DeckConnector, see below |
| `GET /api/config` | Settings. Passwords and the token only show as `wifi_pass_set` and `token_set` |
| `GET /api/backup` | The settings for a restore. `?secrets=1` adds the passwords and the token. Password only |
| `PUT /api/config` | Change settings. Send only the keys you change. The reply has `restart` and `config`, the settings as the DeckConnector stored them |
| `GET /api/screen` | What the deck shows right now, see below |
| `GET /api/screen/<id>.jpg` | One image of that picture |
| `GET /api/screen.jpg` | The whole picture as one JPEG, for a camera in Home Assistant |
| `POST /api/input` | Press a key, turn an encoder or touch the strip, see below |
| `POST /api/menu` | Open the menu on the deck. 409 without a deck |
| `DELETE /api/menu` | Close the menu on the deck, like its close key. It also hides the password, ends the reset question and denies a login request. The deck goes back to the host that draws |
| `POST /api/dock` | Pairing of Companion and Elgato App hosts, see below |
| `DELETE /api/pairing/denied/<ip>` | Take a refused host off the list in `pairing.denied` |
| `POST /api/deck/reconnect` | Switch the deck's USB port off and on, as if the cable were pulled and plugged in |
| `GET /api/log` | The last 16 KB of the log, as text. Each line has the time in UTC. `?boot=prev` gives the log of the boot before this one, also after a crash. It is 404 after a power cut |
| `GET /api/events` | The last 32 events, oldest first: `[{since_s, at, kind, text}]`. `since_s` counts from the start, `at` is the Unix time or null before the clock is set. `kind` is `boot`, `source`, `wlan`, `lan`, `pairing` or `update` |
| `GET /api/coredump` | What crashed last: `task`, `pc`, `ra`, `sp`, `mcause`, `mtval`, `elf_sha256`, `size`. 404 when there is none. `?raw=1` gives the dump itself, for `esp-coredump info_corefile -t raw -c core.elf build/deckconnector.elf` with the ELF of the same build |
| `DELETE /api/coredump` | Clear it |
| `GET /api/boot` | The bootloader (`version`, `idf`, `built`, `sha256`) and the partition table (`partitions`, `md5`, `sha256`). `?raw=bootloader` or `?raw=ptable` gives the region itself. Both are replaced with `POST /api/ota`, see [ota.md](ota.md) |
| `POST /api/ota` | Upload a signed `.dbfw` file as the body. It goes into the slot after the running one, and the DeckConnector restarts into it. `?slot=N` writes into slot N instead, `?boot=0` keeps the running firmware. The running slot is never written |
| `GET /api/slots` | The firmware slots: `slot` (1 to 4), `label`, `state` (`valid`, `pending` until a new start confirms itself, `invalid`, `empty`, `unknown` for an image the bootloader does not track, such as one written before a USB flash), `version`, `built`, `running`, `next` (starts at the next restart), `level` (security level, missing when not known), `bootable` and `why_not` (why `POST /api/slots/N/boot` would refuse it) |
| `POST /api/slots/N/boot` | Restart into slot N. Refused (409) for an `invalid` slot, a slot without a known level or one below the security level, and while the running firmware has not confirmed itself yet |
| `POST /api/sources/probe` | `{"addr": "host:port", "pin": ""}` connects once, like the `satellite` setting would, and reads Companion's greeting. The answer has `ok`, `error` and Companion's `version`. It takes up to 6 s |
| `PUT /api/setup` | `{"step": 3}` keeps the web wizard's step (1 to 6) on the DeckConnector, also over a restart. `{"step": 0}` when the wizard ends. `setup` in the status has it, and the deck shows "Setup 3/6" until setup is done |
| `GET /api/update/check` | `{"current": "v0.1.0", "latest": null, "error": "no releases yet"}`. There are no releases to check yet |
| `POST /api/ota/inspect` | Send the first 144 bytes of a `.dbfw` file. The answer has `target`, `version`, `length`, `sha256`, and `ok` with an `error` when an upload would be refused |
| `POST /api/ota/url` | Send `{"url": "http://..."}` and the DeckConnector loads a signed `.dbfw` file itself. Progress comes over `/api/ws` |
| `POST /api/reboot` | Restart. A new firmware that has not confirmed itself yet counts as confirmed, so it stays |
| `GET /api/frontend` | The web page in use: `source` (`partition` or `builtin`), `api` (the firmware's API version), and for a page from the partition its `version`, `api_min` and `size` |
| `PUT /api/frontend` | Upload a signed `.dbfw` file with target web as the body. It is served at once and kept over restarts. Refused (400) when it is not signed with our key, its hash does not match, or it needs a newer `api` |
| `DELETE /api/frontend` | Go back to the page built into the firmware |
| `GET /api/companion/found` | Companions in the network that announce their Satellite API over mDNS: `name`, `host`, `port`. The first call starts a 2 s search, so ask again after that. A Companion in Docker with bridge networking does not announce itself in the LAN. Enter its address by hand |
| `GET /api/cert` | The certificate HTTPS serves, as a PEM file, without the key. Open to everyone. Import it into a trust store, or pin its SHA-256 (`https_fingerprint` in the status) |
| `PUT /api/tls` | `{"cert": "<PEM>", "key": "<PEM>"}` checks your own certificate (its chain may follow it) and key, and answers `subject`, `issuer`, `not_after` and `sha256`. With `"apply": true` it is kept and the DeckConnector restarts. A key with a password is refused. `https_custom` in the status says it is in use |
| `DELETE /api/tls` | Back to the DeckConnector's own certificate, with a restart |
| `GET /api/input/log` | The last 200 inputs, oldest first: `seq`, `at_ms` (after the start), `origin` (`deck` pressed on the deck, `web` a person over the network, `tool` a script or test), `kind`, `index`, `value`, `x`, `y`, and the `writer` that got it. `?since=<seq>` returns only newer ones. Kept in RAM, gone at a restart |
| `POST /api/sources/retry` | `{"host": "..."}` with a server URL or the Satellite address. The DeckConnector connects to it now instead of after the wait. 404 for anything else. Dock hosts connect by themselves |
| `GET /api/wifi/scan` | Networks in reach, one entry per access point, strongest first: `ssid`, `bssid`, `rssi` (dBm), `channel`, `open`. Only 2.4 GHz. Hidden networks and the setup network are left out. Takes 2 to 3 s |
| `POST /api/wifi/join` | `{"ssid": "...", "pass": "...", "bssid": "..."}` tries that network and answers 202 at once. `network.wlan.join` in the status goes from `testing` to `ok` or `failed` (with `join_error`) within 15 s. Only a network that gives an address is saved, otherwise the old one comes back. `bssid` is optional and locks the DeckConnector to that access point |
| `GET /api/metrics` | CPU, RAM and PSRAM in percent every 5 s for the last 10 minutes, oldest first, and how full the flash is. Kept in RAM only |
| `GET /api/profile` | Time spent per endpoint and CPU share per task since the previous call, and the smallest free stack each task ever had (`stack_free`) |
| `GET /api/tokens` | The API tokens: `name` and `right` |
| `POST /api/tokens` | `{"name": "...", "right": "control"\|"full"}` makes a token and shows it once |
| `DELETE /api/tokens/<name>` | Ends that token |
| `GET /api/ws` | Web socket with live changes, see below |

The routes that act on a deck (screen, input, menu, reconnect) take `?deck=<serial>` from `decks` in the status.
Without it they act on the first deck. A deck that is not attached gets a 404.

## Who controls the deck

Every host that can draw is in one list: Companion over its Satellite API ([satellite.md](satellite.md)), hosts
that use the Network Dock emulation (Companion, the Elgato App), and your own servers
([protocol.md](protocol.md)). Up to 4 servers and up to 4 dock hosts can be connected at once, in any mix.
`sources` in the status is that list.

Two settings pick one entry of it, and they go together. `host` names the entry: a dock host's IP, a server's URL,
or `satellite`. `controller` is the program behind it.

| `controller` | Program |
|---|---|
| `companion` | Bitfocus Companion, over Satellite or as a dock host |
| `app` | The Elgato App, as a dock host |
| `server` | Your own servers |

To pick an entry, send both: `{"controller": "companion", "host": "satellite"}`.

Companion should use its Satellite API, not the Network Dock emulation. A dock host whose `kind` is `companion` can
be moved with `PUT /api/config {"satellite": "<its address>", "controller": "companion", "host": "satellite"}` and
then `POST /api/dock {"forget": "<its address>"}`. When the picked host is not
connected, the first connected one of the same program draws, for `companion` the Satellite connection first.
`controller` alone with an empty `host` means exactly that from the start.

The change works at once. All hosts stay connected, and the deck shows the picked one's picture right away. The
others keep drawing into their own picture in the background and get no input. On the device the hosts are the top
row of the home screen and the menu.

`brightness`, `controller`, `host`, `servers`, `server_pins`, `satellite`, `satellite_pin`, `https_only`,
`cors_origins`, `menu_chord`, `menu_hold_ms` and `qr_login` also work at once. Every other change restarts the DeckConnector,
and the reply says so. `reach` lists where the page finds it after the restart: a fixed LAN address as saved,
the WLAN address while the network stays the same, and the name in `.local`:
`{"ok": true, "restart": true, "reach": ["http://192.168.1.214", "http://deckconnector-a1b2c3.local"]}`.

## Settings

```json
{"name": "deckconnector-a1b2c3", "controller": "companion", "host": "192.168.1.15", "brightness": 80,
 "lan": {"dhcp": true, "ip": "", "mask": "", "gateway": "", "dns": ""},
 "wlan": {"dhcp": true, "ip": "", "mask": "", "gateway": "", "dns": ""},
 "wifi_ssid": "deckbridge", "wifi_pass_set": true,
 "servers": ["ws://192.168.1.128:8765/deckbridge", "wss://example.org/deckbridge", "", ""],
 "server_pins": ["", "", "", ""], "satellite": "192.168.1.15", "satellite_pin": "",
 "https_only": false, "cors_origins": "", "menu_chord": "auto", "menu_hold_ms": 2000, "usb_speed": "auto",
 "token_set": false}
```

`menu_chord` says which two controls open the menu on the deck when held together for `menu_hold_ms` (500 to
10000): `auto` (the outer encoders, or the first and last key on a deck without encoders), `keys` (first and last
key), `encoders` (the outer encoders, like `auto`), or `off`.

`usb_speed` is `auto` or `full`. `auto` starts at high speed. When a USB device is there but no deck comes up,
for example behind a hub that drops it, the port switches to full speed until the next restart. `full` always
uses full speed. Full speed moves about 64 KB of pictures per second instead of several MB, so a full page takes
up to a second.

Until setup is done, the deck shows a QR code with `http://<address>/?t=<code>`. Opening it logs that browser in
once, without the password. A code is good for 10 minutes and for one login. Then the deck shows a new one.
`qr_login: false` turns it off.

`ntp_server` sets the clock. Empty uses the one the DHCP server names, then `pool.ntp.org`.

To change them, `PUT` the same keys. The secret ones are `wifi_pass`, `token` and `admin_pass`. `admin_pass`
has 6 to 23 characters (bytes, so fewer for letters outside ASCII). `wifi_pass` has 8 to 63 characters, or 64 hex
digits, or is empty for an open network. `name` has 1 to 31 letters, digits and dashes, with no dash at the start
or end, because it is also the host name and the mDNS name. `servers` holds up to 4 `ws://` or `wss://` URLs
without spaces. An empty list finds a server over mDNS. A fixed address needs `ip` and `mask`. The mask is
`128.0.0.0` to `255.255.255.252`, `ip` is not the network or broadcast address, and `gateway` is empty or another
address in the same network. A refused value gets 400 with the reason, and nothing is saved. `satellite` is the address of Companion for its Satellite API, `host` or
`host:port`, port 16622 when left out. A `ws://` or `wss://` URL goes to Companion's WebSocket port 16623, or to
a proxy in front of it. Empty turns it off.

A `wss://` address must show a certificate from one of the 41 most used public CAs, made out to its host name.
For any other certificate, put its SHA-256 into `server_pins` (same position as the URL in `servers`) or
`satellite_pin`. Then only that certificate counts, and the name in it does not matter. `openssl x509 -noout
-fingerprint -sha256` prints it, colons are fine. The DeckConnector never talks TLS to a host it cannot check.

The secret keys never come out of `GET /api/config`. So its reply is a backup without passwords, and a `PUT` of
it restores the settings. `GET /api/backup?secrets=1` (password only) adds `admin_pass`, `wifi_pass` and `token`, so
a `PUT` of it restores those too. API tokens, paired hosts and the TLS key are never in it. Make new tokens, pair
again and upload the key again.

## Status

```json
{"via_ap": false, "id": "a0b1c2a1b2c3", "name": "deckconnector-a1b2c3", "firmware": "v0.1.0", "wlan_firmware": "3.0.10", "uptime_s": 23,
 "boot": {"reason": "restart", "at": 1794012139},
 "setup": {"active": false, "step": 0, "rerun": false}, "controller": "companion", "writer": "dock2", "menu_open": false,
 "device": {"model": "Stream Deck +", "serial": "EL00K1A00001", "firmware": "2.0.3.7", "keys": 8, "encoders": 4,
            "width": 800, "height": 480},
 "network": {"lan": {"cable": true, "dead": false, "ip": "192.168.1.214"},
             "wlan": {"ssid": "deckbridge", "ip": "192.168.1.237", "bssid": "aa:bb:cc:dd:ee:ff", "channel": 1,
                      "lock": "", "rssi": -48, "join": "", "join_error": ""},
             "ap": {"on": false, "ssid": ""}},
 "sources": [{"kind": "server", "host": "ws://192.168.1.128:8765/deckbridge", "name": "", "connected": false,
              "chosen": false, "drawing": false},
             {"kind": "companion", "host": "192.168.1.15", "name": "studio", "connected": true, "chosen": true,
              "drawing": true}],
 "counters": {"images": 7, "superseded": 0, "rejected": 0, "usb_errors": 0, "usb_speed": "high", "inputs": 0,
 "inputs_dropped": 0},
 "boot_ms": {"deck": 537, "lan_link": 2688, "wlan_ip": 5724, "lan_ip": 6692, "companion": 7439, "host_draws": 7665},
 "memory": {"heap_free": 353203, "heap_min_free": 290191, "psram_free": 32211772, "heap_largest": 208896,
            "psram_largest": 30932992, "tasks": 37, "sockets": 9, "sockets_max": 32}}
```

- `menu_open` is true while the menu, the password, the reset question or a login request covers the deck. `via_ap` is true when this request came over the setup
  access point. The web socket's `state` has `menu_open` too.
- `writer` is who draws at this moment: `ui`, `server1` to `server4`, `dock1` to `dock4`, or `satellite`. The
  source with `drawing` in `sources` says which host that is. It is `ui` while the menu is open, an update runs,
  or the picked host is not connected.
- `boot_ms` says when each step of this boot happened first, in ms after the firmware started: the deck, the
  LAN link, each address, Companion, the Elgato App, a server, the first picture of a host, and Companion
  over its Satellite API.
- `setup.step` is the web wizard's step, 0 when it does not run. `setup.active` is true until setup is done or
  while the wizard runs. `setup.rerun` is true while it runs again after setup was done.
- `boot.reason` says why the DeckConnector started: `power`, `pin` (the reset button), `restart`, `crash`,
  `watchdog`, `brownout` or `other`. `boot.at` is the start time in Unix seconds, `null` until the clock is set.
- `heap_largest`, `psram_largest`, `tasks` and `sockets` show fragmentation and leaks over a long run.
- `heap_free` includes the LP RAM. `heap_lp_free` is the LP RAM part. The 16 KB log ring that survives a restart
  lives there.
- `id` is the DeckConnector's MAC address. It stays when the name changes.
- `device` is `null` without a Stream Deck. `decks` lists every attached deck with the same fields, its `number`
  (from 1), its `usb_speed` and `hub` (true when it hangs on a USB hub).
- `sources` lists every server that is set or was found, Companion's Satellite API when an address is set (with
  `"via": "satellite"`), and every Network Dock host. A dock host's `kind` is
  `unknown` until it asks something, and it is `connected` once it also uses the deck. `name` is what the router
  calls the host, empty when it has no name or is a server. Servers and the Satellite address also have `error`
  (why the last try failed, empty while connected), `since_s` (when this state began, in seconds after the start, to compare with `uptime_s`) and `iface` (`lan` or
  `wlan` while connected). `chosen` marks the one that draws whenever no menu
  covers it.
- `network.wlan.lock` is the access point the DeckConnector is locked to (`wifi_bssid`), empty for any. `join`
  is the state of the last `POST /api/wifi/join`. `network.ap.on` says the setup WLAN is open. A page reached over
  it loses the DeckConnector when the setup network closes.
- `superseded` counts images that a newer one replaced before they were drawn.
- `usb_errors` counts failed USB transfers. Transfers that an unplug ends are not counted.
- `usb_speed` is `high` or `full`, the speed the USB port runs at now.

## What the deck shows

```json
{"writer": "dock2", "width": 800, "height": 480,
 "keys": [{"x": 13, "y": 12, "w": 120, "h": 120}, ...], "touch": [{"x": 0, "y": 380, "w": 800, "h": 100}],
 "encoders": 4, "menu_chord": {"on": "encoders", "first": 0, "last": 3, "hold_ms": 2000},
 "images": [{"x": 13, "y": 12, "w": 120, "h": 120, "url": "/api/screen/9.jpg"},
            {"x": 232, "y": 12, "w": 120, "h": 120, "url": "/api/screen/10.jpg"}]}
```

`keys` lists the keys in index order, `touch` the touch strips. `menu_chord` names the two encoders or keys
(`on` is `encoders` or `keys`) that open the menu when held together for `hold_ms`. `on` is `off` when no
pair opens it.

Draw the JPEGs in this order at their place to get the picture. On the Stream Deck + the keys are at their place
on the 800 x 480 screen, and the touch strip is the bottom 100 pixels. An image keeps its URL until something
covers it, so a browser may cache it. A URL that is gone answers 404. Without a Stream Deck, `width` and `height`
are missing and `images` is empty.

## Input

`POST /api/input` acts as if you used the deck. Keys and encoders count from 0. Coordinates are those of
`/api/screen`.

Companion, the Elgato App and the Satellite connection get the input only with `"real"`, because their
buttons switch real devices. `"real": true` is for a person who pressed something, as the web UI does. A script
names the host instead, `"real": "192.168.1.20"`, and the press goes out only if that host draws at that moment.
Without `"real"`, only the DeckConnector's own screens and your own servers get the input.
`simulated_input: "local"` in the status says the firmware works this way.

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
The menu opens with `down` on the two controls in `menu_chord` and `up` after its `hold_ms`. A press on the real
deck in between ends the simulated hold, because the deck always reports all keys at once.

On the DeckConnector's own pages, input sent over the API can open pages and pick a host. Restart, reset, the LAN
settings, the setup WLAN and the keys that let a host in work only on the deck itself.

## Pairing

Companion and the Elgato App connect without a password. The DeckConnector lists every host that connected,
by its MAC address, so a laptop that gets a new address from DHCP stays the same host. With `dock_paired_only` set in
the settings, only listed hosts get in. A new host is then let in and listed while none is known and while pairing
is open. So the first host gets in by itself, and later ones need pairing. Otherwise it is turned away, and the deck
and the web UI ask whether to let it in. Up to 8 hosts are listed. A refused host is not remembered. Its next try asks again, so a restarted app gets a new question.

`pairing` in the status has `only_paired`, the waiting `request` or `null`, `open` while new hosts get in,
`auto_open` while no host is listed yet (the first one gets in without pairing),
`open_s` how long still, `denied` with the hosts refused since the start (`ip`, `name`, `since_s`, at most 4,
newest last) and `hosts` with `ip`, `mac`, `name`, `kind` and `connected`. `name`, and
`request_name` for the waiting host, is what the router's DNS answers for the address, empty when it does not
know one. An answer names the host that asks (`request`). When another host asks by then, or none, it gets 409.
`POST /api/dock` takes one of these:

```json
{"accept": true, "ip": "192.168.1.15"}
{"accept": false, "ip": "192.168.1.15"}
{"pairing": 300}
{"pairing": 0}
{"forget": "192.168.1.15"}
{"forget": true}
```

`DELETE /api/pairing/denied/<ip>` takes a host off the `denied` list.

## Live changes

`/api/ws` is a web socket with the same login as the other endpoints. A browser page from another site is
refused. Every message is one JSON object, and `t` says what it is.

| `t` | When |
|---|---|
| `state` | At once on connect, then on every change. The status without uptime, signal and counters |
| `screen` | The picture changed. `GET /api/screen` or `/api/screen.jpg` has the new one |
| `input` | Someone used the deck: `kind`, `index`, `value`, `x`, `y`, `x2`, `y2`, the `writer` that got it, and `simulated` (an API press without `"real"` or bound to a host). A token never gets simulated presses |
| `ota` | Progress of an update: `target`, `state`, `pct`, and `error` when it failed |
| `ping` | Every 5 s. Without any message for 12 s the connection is dead, so connect again |
| `log` | Only after `{"t": "log"}`: new log text in `text`. The first ones carry everything still kept, the boot included |

To change something, send `{"t": "set", "id": 1, ...}` with the keys of `PUT /api/config`, or
`{"t": "input", "id": 2, ...}` with the keys of `POST /api/input`. The answer is
`{"t": "result", "id": 1, "status": 200, "reply": {...}}`.

`{"t": "log", "id": 3}` starts the log on this connection. It needs the password or a `full` token.
`uv run python -m dbtools.netlog --secs 60` follows it from a shell.
