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

## Demo

`npm run build:demo` builds this page with a mock of the device in `src/demo` (only `fetch` and the web socket are
replaced). `npm run e2e:demo` clicks through it. Three checks keep the mock true to the firmware:

- `src/demo/routes.test.ts` reads the route table in `firmware/components/db_web/http.c`. It fails when the page calls a route the mock or the firmware lacks, or when the mock has a route the firmware lacks.
- Every mock reply is typed with the return type of its `api.*` call in `src/api/client.ts`, so `tsc` fails when a shape drifts.
- `e2e-demo/contract.spec.ts` sends the main GET requests to the mock and to `firmware/simdeck` and compares keys and value types. A key only the mock sends must appear in the firmware sources.

## Upload to the device

```
cd ../tools
uv run python -m dbtools.fw web
```

It signs `dist/index.html` and uploads it. A firmware build embeds `dist/index.html` and stops without it, so run
`npm run build` before building the firmware.
