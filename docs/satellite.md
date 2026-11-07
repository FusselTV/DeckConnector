# Companion over its Satellite API

The DeckConnector can connect to Bitfocus Companion itself, on TCP port 16622. It then offers the attached deck
the way Companion Satellite does. Set the address in the web UI, or `PUT /api/config` with
`{"satellite": "192.168.1.10"}` (`host:port` for another port). Pick it with
`{"controller": "companion", "host": "satellite"}`, or tap it in the list.

It needs Satellite API 1.9 or newer. It was tested with Companion 5.2. Companion does not have to find the
DeckConnector, so it also works from Docker without host networking, as long as Companion's port is reachable.

## Over WebSocket

Companion also takes the Satellite API on its WebSocket port 16623. Give the address as a URL,
`ws://192.168.1.10:16623`. A reverse proxy that already serves Companion with TLS can pass a path on to that
port. Then the address is `wss://companion.example.org/satellite` and nothing travels in clear text. The
certificate check is the same as for servers, see `satellite_pin` in [api.md](api.md).

## Layout

The DeckConnector gives Companion the same rows and columns as Companion's own deck plugin. On a Stream
Deck + that is two rows of keys, then the touch strip in four parts of 200 x 100, then the four encoders. Your pages
look the same as over USB or the Network Dock. The surface id is `streamdeck:<serial>`, the one the plugin uses,
so Companion keeps the deck's settings.

A tap on the strip presses the part under the finger. A swipe sideways turns the page. A swipe up or down turns
that part like an encoder.

## Pictures

Companion sends PNG. The DeckConnector turns each one into the JPEG the deck takes. It keeps up to 4 MB of these
JPEGs, and Companion then names a picture it sent before instead of sending it again. So a page you have seen comes
back at once.

## After a power cut

Companion keeps the session of the last boot open while it still writes to it, and the deck stays taken until
that session ends. The DeckConnector ends it right after the start, the same way as for the Elgato App (see
[dock.md](dock.md)). Companion has the deck again about 10 s after power comes back.

## Both at once

The DeckConnector stays a Network Dock as well. When Companion connects as a dock while no Satellite address is set,
the DeckConnector tries Companion's Satellite API on the same address. If it answers, the DeckConnector saves that
address and moves Companion to Satellite. Companion's dock connection then ends, and it cannot connect as a dock
again while the address points to it. If you remove the address or say no on the deck, this stops until the next
restart. If Companion still uses both, it sees two surfaces of the same deck. The one that connects second gets the
id `streamdeck:<serial>-dev2` and its own settings.

## Limits

- One Companion.
- On a Stream Deck Neo only the eight keys, not the small screen and the two touch keys.
