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

## Add a setting

- `db_config.h`: append the field to `db_config_t`, never insert. `db_config.c`: `CONFIG_VERSION` and its migration,
  `defaults()`, `db_config_to_json`, `take_scalars`.
- `api.c` `config_edit`: copy the field into `live` when it applies at once. Without that line every change of it
  restarts the bridge.
- `main.c` `write_status` when Home Assistant needs it. It reads `/api/status`, not the config.
  `firmware/sim/main/main.c` has its own copy of the status.
- Web UI: `api/types.ts`, `WRITABLE` in `lib/backup.ts` and `RESTARTS` when it restarts, the config, status and
  `LIVE` of the demo in `demo/model.ts`, the page, the restore labels in `pages/Firmware.tsx`, both `i18n` files.
- Home Assistant: the entity in `custom_components/deckconnector`, `translations/*.json`, `icons.json`,
  `STATUS` in `tools/ha/tests/conftest.py`.
- Docs: `site/content/docs/api.mdx` and `api.de.mdx`.

`web/e2e-demo/contract.spec.ts` fails when the JSON, the demo or the live lists miss the field.

## Add a deck model

- The driver: a row in `models` of `elgato_gen2.c`. The Mini family is in `elgato_gen1.c`, Mirabox decks in
  `mirabox.c`. A new vendor is one file in `db_core/drivers` and one line in `db_driver.c`.
- `db_cora_model()` in `db_cora.c`: the name the Network Dock reports.
- `db_quirks.c` when the deck needs other wait times. `pad()` in `screens.c` when its grid has no PIN pad.
- Host tests: `deck_models` in `host_test/deck_emu.c`, the hash per PID in `test_sat.c`, `decks` in `test_ui.c`.
- simdeck: `MODELS` in `tools/dbtools/simdeck.py`, `EXPECT` in `tools/simdeck/test_models.py`, the lists in
  `tools/simdeck/conftest.py`.
- Web UI: `MODELS` and `modelOf()` in `web/src/deck/models.ts`.
- Site: `hardware.mdx` and `matter.mdx` in both languages, `site/components/compare/data.ts`,
  `site/components/landing/copy.ts`.

## Tests

Most tests need no board. They run against the firmware built for Linux. `firmware/sim` has the web API, and
`firmware/simdeck` has the whole firmware with emulated decks of every model. Building them needs `libbsd-dev` and
`libjpeg-dev`. They carry the web UI from `web/dist`, so build that first (`cd web && npm run check`).

| What | Command |
|---|---|
| `db_core`, with AddressSanitizer | `cmake -S firmware/host_test -B build-host -G Ninja && cmake --build build-host && ctest --test-dir build-host` |
| Firmware after an edit | `tools/build.sh` |
| Web UI checks | `cd web && npm run check` |
| Simulators | `cd firmware/sim && idf.py build`, the same in `firmware/simdeck` |
| API on the simulator | `cd tools && uv run pytest tests --target sim -n 4` |
| Web UI end to end | `cd web && npm run e2e` |
| Every deck model, Matter | `cd tools && uv run pytest simdeck` |
| mDNS | `cd tools && uv run pytest simnet` |
| Home Assistant | `cd tools/ha && uv run pytest` |
| Home Assistant in Docker | `cd tools/ha && uv run pytest e2e -p no:homeassistant` |

The deck model tests need `cd tools/companion-check && npm ci` once. Matter pairing and its mDNS records still need a
board. The suites use fixed ports. To run two at once, start each with `.github/scripts/netns.sh`. That and the mDNS
tests need unprivileged user namespaces, which Ubuntu 24.04 turns off; `unshare -rn true` checks it. Tests that wait
for a timer can fail when the PC is busy; run them again alone.

With a board, set its address as `host` in `~/.config/deckbridge/bench.json` and its admin password in
`~/.config/deckbridge/admin_pass`. The other keys are in
`tools/dbtools/site.py`. Tests that need a camera, a switch or a test Companion skip until those are set.

```
cd tools && uv run pytest tests            # end to end
cd tools && uv run pytest tests -m ota     # updates, restarts the board
```

## CI

A pull request runs only the jobs its files can affect. `ci-ok` is the check that must pass.

| Workflow | Runs on changes in | Jobs |
|---|---|---|
| ci | `firmware`, `web`, `tools`, `.github` | firmware build and clang-tidy, host tests, lint, web UI, demo, the board-free suites, size check |
| home-assistant | `custom_components`, `tools/ha`, `hacs.json`, `firmware`, `web` | hassfest, HACS, tests, Home Assistant in Docker |
| site | `site`, `web`, the deck screens | build, Lighthouse |

## Update files

```
cd tools
uv run python -m dbtools.dbfw pack --target p4 --version "$(git describe)" ../firmware/build/deckconnector.bin fw.dbfw
```

Upload `fw.dbfw` in the web UI. On a board set up for the tests, `uv run python -m dbtools.fw deploy` signs the build,
uploads it into slot 3 or 4 and starts it. Slots 1 and 2 stay as they are.

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
