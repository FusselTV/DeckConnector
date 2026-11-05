# Companion over its Satellite API

The bridge can connect to Bitfocus Companion itself, on TCP port 16622. It then offers the attached deck the
way Companion Satellite does. Set the address in the web UI, or `PUT /api/config` with
`{"satellite": "192.168.1.10"}` (`host:port` for another port). Pick it with `controller` `companion`.

It needs Satellite API 1.9 or newer. It was tested with Companion 5.2. Companion does not have to find the
bridge, so it also works from Docker without host networking, as long as port 16622 is reachable.

## Layout

The bridge gives Companion the same rows and columns as Companion's own Stream Deck plugin. On a Stream Deck +
that is two rows of keys, then the touch strip in four parts of 200 x 100, then the four encoders. Your pages
look the same as over USB or the Network Dock. The surface id is `streamdeck:<serial>`, the one the plugin uses,
so Companion keeps the deck's settings.

A tap on the strip presses the part under the finger. A swipe sideways turns the page. A swipe up or down turns
that part like an encoder.

## Pictures

Companion sends PNG. The bridge turns each one into the JPEG the deck takes. It keeps up to 4 MB of these JPEGs,
and Companion then names a picture it sent before instead of sending it again. So a page you have seen comes
back at once.

## After a power cut

Companion keeps the session of the last boot open while it still writes to it, and the deck stays taken until
that session ends. The bridge ends it right after the start, the same way as for the Stream Deck app (see
[dock.md](dock.md)). Companion has the deck again about 10 s after power comes back.

## Both at once

The bridge stays a Network Dock as well. If Companion uses both, it sees two surfaces of the same deck. The one
that connects second gets the id `streamdeck:<serial>-dev2` and its own settings. Use one of the two.

## Limits

- One Companion.
- On a Stream Deck Neo only the eight keys, not the small screen and the two touch keys.
