# Tools

Python tools and tests, run with `uv run` from this folder. Bench values come from
`~/.config/deckbridge/bench.json` (`dbtools/site.py`). Test commands are in
[CONTRIBUTING](../.github/CONTRIBUTING.md).

| Path | What it is |
|---|---|
| `dbtools/server.py`, `protocol.py` | a small server for the own-server protocol |
| `dbtools/dbfw.py`, `fw.py`, `flashimage.py` | signed update files, the bench update loop, the files for the web flasher |
| `dbtools/safety.py`, `benchlock.py` | the input gate every press goes through, one bench user at a time |
| `dbtools/satfake.py` | a fake Companion Satellite server |
| `dbtools/load.py`, `soak.py`, `bench.py`, `pagebench.py`, `deckmeter.py` | load, soak and timing measurements, `deckmeter.py` with the camera |
| `dbtools/serlog.py`, `netlog.py`, `improv.py`, `poe.py` | serial log, network log, Improv over serial, a PoE power cut |
| `dbtools/gen_font.py` | writes `firmware/components/db_ui/font_data.c` |
| `tests/` | end to end tests against the board, or against `firmware/sim` with `--target sim` |
| `ha/` | tests of the Home Assistant integration in `tests/`, a Home Assistant in Docker in `e2e/` |
| `companion-check/` | drives the Dock path with the library inside Bitfocus Companion |
| `media/` | re-records the README and docs pictures, see its [README](media/README.md) |
| `baselines/` | earlier `load.py --json` runs to compare against |
| `build.sh`, `flash.sh` | a quick firmware build, flashing over the serial bridge |
