# Home Assistant

What a Home Assistant integration finds in the Deck-Connector's API, and how it maps to entities. The API itself is
in [api.md](api.md).

## Finding and logging in

The config flow finds the Deck-Connector over zeroconf as `_deckbridge-api._tcp`. It asks for a token, made in
the web UI under System. Requests send it as `Authorization: Bearer`. `id` in `/api/status` is the
`unique_id`. It is the MAC address and stays when the name changes. With `https_only` on, the integration has
to use `https://` on port 443 and accept the Deck-Connector's own certificate.

## Live data

One web socket to `/api/ws` carries every change. `state` arrives at once and then on each change, `screen` when
the picture changes, `input` for every press and turn, and `ota` during an update. A `ping` comes every 5 s,
so 12 s without a message means the link is dead and the integration connects again. Polling
`/api/status` every 30 s adds the counters and uptime, and `/api/metrics` the load.

## Entities

| Entity | Source |
|---|---|
| Camera "Deck" | `GET /api/screen.jpg`, fetched again on each `screen` message |
| Select "Controller" | `controller` and `host` in `PUT /api/config`, options from `sources` in the status |
| Light or number "Brightness" | `brightness` in the settings, 0 to 100 |
| Binary sensors per source | `connected` and `drawing` of each entry in `sources` |
| Binary sensor "Stream Deck" | `device` is not `null` |
| Sensors | `network.lan.ip`, `network.wlan.rssi`, `uptime_s`, the counters, and CPU and RAM from `/api/metrics` |
| Event "Deck input" | `input` messages: `kind` is `key_down`, `key_up`, `encoder_turn`, `touch_tap` and so on, with `index` and `value` |
| Buttons | `POST /api/menu`, `POST /api/reboot`, `POST /api/deck/reconnect` |
| Update | `firmware` in the status as the installed version, `POST /api/ota/url` to install, `ota` messages for progress |

Key presses go to the host that draws, and Home Assistant sees them as events too. Presses that a test or
script sends without `"real"` never reach a token, so automations only run on real ones. To press a key from an
automation, send `{"t": "input", "key": 3}` over the web socket or `POST /api/input`.

## Not there yet

- No list of released firmware. An update entity can only install a URL it gets from elsewhere.
- The LAN settings from DHCP (mask, gateway, DNS) are not in the status.
- A key cannot be drawn from Home Assistant. Drawing needs a server connection, see [protocol.md](protocol.md).
