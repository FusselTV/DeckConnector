# Contributing

How to build the firmware is on the website, under [From source](https://deckconnector.com/docs/flashing#from-source).
All user docs are there too. This page is for working on the code.

| Folder | What it is |
|---|---|
| `firmware/` | the ESP-IDF project, with host tests in `firmware/host_test`, a Linux build of the web code in `firmware/sim` and the Matter image in `firmware/matter` |
| `web/` | the web UI, built into the firmware ([web/README.md](../web/README.md)) |
| `site/` | deckconnector.com with the docs, the demo and the flasher ([site/README.md](../site/README.md)) |
| `tools/` | Python tools: a reference server, signing, flashing, the end to end tests |

## Firmware

| Component | What it is |
|---|---|
| `db_core` | plain C without ESP-IDF: device model, deck drivers, image queue, protocol, Cora framing, Satellite lines, layout, PNG |
| `db_board` | pins and features per board, picked in menuconfig |
| `db_config` | settings in NVS |
| `db_deck` | USB host port, JPEG encoder, and the deck service: who may draw, writer task, input |
| `db_net` | Ethernet, Wi-Fi, setup access point, mDNS |
| `db_link` | the WebSocket servers and the Companion Satellite client |
| `db_dock` | the Network Dock emulation |
| `db_ui` | status screens, menu and font on the deck |
| `db_ota` | signed updates and rollback, the `.dbfw` header is in `db_ota.c` |
| `db_web` | web UI, JSON API, captive DNS |
| `db_audio` | the speaker: sounds and the self-test |
| `db_log` | the log ring kept over a restart, boot time marks |
| `db_serial` | Improv over USB serial, for the flasher |

`idf.py -C firmware/matter build` builds the Matter image from the same components.

`db_core` compiles on a PC, and its tests run with AddressSanitizer. A new model of a known deck family is one table
row in its driver. A new vendor is one driver file and one line in `db_driver.c`.

One source draws on the deck at a time. The menu and an update come first, then the chosen host, else the home
screen. `db_deck` keeps the newest image per area for every source, so a host that gets the deck back shows at once.

## Tests

```
cmake -S firmware/host_test -B build-host -G Ninja && cmake --build build-host && ctest --test-dir build-host
```

The API tests also run without a board, against the web code built for Linux. Building it needs `libbsd-dev`.

```
cd firmware/sim && idf.py build
cd tools && uv run pytest tests --target sim -n auto
```

With a board, set its address as `host` in `~/.config/deckbridge/bench.json`. The other keys are in
`tools/dbtools/site.py`. Tests that need a camera, a switch or a test Companion skip until those are set.

```
cd tools && uv run pytest tests            # end to end
cd tools && uv run pytest tests -m ota     # updates, restarts the board
```

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
