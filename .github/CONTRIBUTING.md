# Contributing

The user docs are on the [website](https://deckconnector.com/docs). This page is for working on the code.

## Build

Needs ESP-IDF 6.0.2, Node.js and [uv](https://docs.astral.sh/uv/).

```bash
git clone https://github.com/FusselTV/DeckConnector.git
cd DeckConnector/web && npm ci && npm run build
cd ../tools && uv run python -m dbtools.dbfw keygen
cd ../firmware && idf.py build && idf.py -p /dev/ttyACM0 flash
```

The web UI is part of the firmware, so it builds first. `keygen` runs once. It keeps the private key in
`~/.config/deckbridge/ota_key.pem` and writes the public key into the firmware. A connector takes only updates signed
with that key.

| Folder | What it is |
|---|---|
| `firmware/` | the ESP-IDF project ([firmware/README.md](../firmware/README.md)) |
| `web/` | the web UI, built into the firmware ([web/README.md](../web/README.md)) |
| `site/` | deckconnector.com with the docs, the demo and the flasher ([site/README.md](../site/README.md)) |
| `tools/` | Python tools: a reference server, signing, flashing, the end to end tests ([tools/README.md](../tools/README.md)) |

## Firmware

`db_core` compiles on a PC, and its tests run with AddressSanitizer. A new model of a known deck family is one table
row in its driver. A new vendor is one driver file and one line in `db_driver.c`.

One source draws on the deck at a time. The menu and an update come first, then the chosen host, else the home
screen. `db_deck` keeps the newest image per area for every source, so a host that gets the deck back shows at once.

## Tests

Most tests need no board. They run against the firmware built for Linux. `firmware/sim` has the web API, and
`firmware/simdeck` has the whole firmware with emulated decks of every model. Building them needs `libbsd-dev` and
`libjpeg-dev`. They carry the web UI from `web/dist`, so build that first (`cd web && npm run check`). Times are from
a 12-core PC with a warm ccache.

| What | Command | Time |
|---|---|---|
| `db_core` | `cmake -S firmware/host_test -B build-host -G Ninja && cmake --build build-host && ctest --test-dir build-host` | 1 min |
| Firmware after an edit | `tools/build.sh` | 5 s |
| Web UI checks | `cd web && npm run check` | 10 s |
| Simulators | `cd firmware/sim && idf.py build`, the same in `firmware/simdeck` | 10 s each |
| API on the simulator | `cd tools && uv run pytest tests --target sim -n 4` | 2.5 min |
| Web UI end to end | `cd web && npm run e2e` | 4 min |
| Every deck model | `cd tools && uv run pytest simdeck` | 5 min |
| Home Assistant | `cd tools/ha && uv run pytest` | 15 s |
| Home Assistant in Docker | `cd tools/ha && uv run pytest e2e -p no:homeassistant` | 30 s |

The deck model tests also need `cd tools/companion-check && npm ci` once. The suites use fixed ports. To run two at
once, for example from two checkouts, start each in a network namespace of its own:
`.github/scripts/netns.sh uv run pytest simdeck`. This needs unprivileged user namespaces, which Ubuntu 24.04 turns
off by default. Tests that wait for a timer can fail when the PC is busy; run them again alone.

`firmware/simdeck` runs the whole firmware on emulated decks, Matter included. Its Matter stack is a fake, so
pairing and the Matter mDNS records still need a board. The mDNS tests put both simulators in a network namespace
of their own, and nothing reaches the LAN. This needs unprivileged user namespaces. `unshare -rn true` checks it.

```
cd firmware/simdeck && idf.py build
cd tools && uv run pytest simdeck    # every deck model, Matter
cd tools && uv run pytest simnet     # mDNS
```

With a board, set its address as `host` in `~/.config/deckbridge/bench.json`. The other keys are in
`tools/dbtools/site.py`. Tests that need a camera, a switch or a test Companion skip until those are set.

```
cd tools && uv run pytest tests            # end to end
cd tools && uv run pytest tests -m ota     # updates, restarts the board
```

## CI

A pull request runs only the jobs its files can affect. `ci-ok` is the check that must pass.

| Workflow | Runs on changes in | Jobs | Time |
|---|---|---|---|
| ci | `firmware`, `web`, `tools`, `.github` | firmware build and clang-tidy, host tests, lint, web UI, demo, the board-free suites, size check | 9 min |
| home-assistant | `custom_components`, `tools/ha`, `firmware` | hassfest, HACS, tests, Home Assistant in Docker | 3 min |
| site | `site`, `web`, the deck screens | build, Lighthouse | 7 min |

## Update files

```
cd tools
uv run python -m dbtools.dbfw pack --target p4 --version "$(git describe)" ../firmware/build/deckconnector.bin fw.dbfw
```

Upload `fw.dbfw` in the web UI. On the bench, `dbtools.fw` does it in one step:

```
tools/build.sh                              # incremental build without idf.py
cd tools
uv run python -m dbtools.fw deploy          # sign the build, upload it into a bench slot and start it
uv run python -m dbtools.fw ab REV_A REV_B  # build both, start A, B waits in the other bench slot
uv run python -m dbtools.fw web             # sign web/dist/index.html and serve it from www
```

Raise `SECURITY_LEVEL` in `db_ota.c` with a security fix, and `DB_WEB_API` in `db_web.h` when a route or field the
page relies on changes. `tools/flash.sh all` writes everything over USB when a board no longer starts.

## Crashes

`GET /api/coredump?raw=1` saves the last crash. Read it with the ELF of the same build:

```
esp-coredump info_corefile -t raw -c core.elf firmware/build/deckconnector.elf
```

`idf.py -DDB_DEBUG_CRASH=1 build` makes a test build that can crash on purpose.

## Releases

Push a signed tag `vX.Y.Z`. The release workflow builds and signs the firmware and makes the GitHub release. Then
the site deploys again and the flasher offers the new firmware. A tag like `vX.Y.Z-rc1` makes a pre-release, which
the flasher does not offer.
