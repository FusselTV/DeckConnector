# Contributing

How to build the firmware is on the website, under [From source](https://deckconnector.com/docs/flashing#from-source).
[docs/architecture.md](../docs/architecture.md) shows where things are.

| Folder | What it is |
|---|---|
| `firmware/` | the ESP-IDF project, with host tests in `firmware/host_test` and a Linux build of the web code in `firmware/sim` |
| `web/` | the web UI, built into the firmware ([web/README.md](../web/README.md)) |
| `site/` | deckconnector.com with the docs, the demo and the flasher ([site/README.md](../site/README.md)) |
| `tools/` | Python tools: a reference server, signing, flashing, the end to end tests |
| `docs/` | developer docs: protocols, update file format, Network Dock emulation |

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

Upload `fw.dbfw` in the web UI. The format is in [docs/ota.md](../docs/ota.md).

## Releases

Push a signed tag `vX.Y.Z`. The release workflow builds and signs the firmware and makes the GitHub release. Then
the site deploys again and the flasher offers the new firmware. A tag like `vX.Y.Z-rc1` makes a pre-release, which
the flasher does not offer.
