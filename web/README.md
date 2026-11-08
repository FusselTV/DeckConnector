# DeckConnector web page

The web page the DeckConnector serves: status, live deck, sources, network, access, firmware and diagnostics. It
builds into one file, `dist/index.html`, with all scripts, styles and icons inside.

## Develop

```
npm install
npm run sim     # the host simulator from firmware/sim, on port 18200
npm run dev     # the page on http://localhost:5173, /api goes to the simulator
```

Log in with `simulator-pass`. To look at the real device, start the dev server with
`DC_TARGET=http://192.168.1.214 npm run dev`. Against anything but the local simulator the dev server is read-only.
It refuses every request that changes the device, and remote control stays off.

## Check and build

```
npm run check   # types, lint, format, unit tests, build with the size budget
npm run e2e     # browser tests against a fresh simulator
```

The build fails above 200 KB gzip.

## Upload to the device

```
cd ../tools
uv run python -m dbtools.fw web
```

It signs `dist/index.html` and uploads it. A firmware build embeds `dist/index.html` and stops without it, so run
`npm run build` before building the firmware.
