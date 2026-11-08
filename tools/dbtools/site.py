"""This bench's own values, from ~/.config/deckbridge/bench.json (DECKBRIDGE_BENCH names another file).

Keys: host, camera, satellite_test, production, serial, bench_dir, unifi {url, key_file, switch_id, port,
dock_port}. Missing keys get the defaults below; a tool that needs an unset one stops with need().
uv run python -m dbtools.site KEY   prints one value, for shell scripts.
"""

import json
import os
import sys
from pathlib import Path

FILE = Path(os.environ.get("DECKBRIDGE_BENCH", Path.home() / ".config/deckbridge/bench.json"))
_conf = json.loads(FILE.read_text()) if FILE.exists() else {}

HOST: str = os.environ.get("DECKBRIDGE_HOST") or _conf.get("host", "")
CAMERA: str = os.environ.get("DECKBRIDGE_CAMERA") or _conf.get("camera", "")
SATELLITE_TEST: str = os.environ.get("DECKBRIDGE_SATELLITE") or _conf.get("satellite_test", "")
PRODUCTION: dict = {"controller": "companion", "host": "satellite", "satellite": "", **_conf.get("production", {})}
SERIAL: str = _conf.get("serial", "")
BENCH_DIR = Path(_conf.get("bench_dir", Path.home() / "deckbridge-bench")).expanduser()
UNIFI: dict = _conf.get("unifi", {})


def need(value, key: str):
    if not value:
        raise SystemExit(f"set {key} in {FILE}")
    return value


if __name__ == "__main__":
    print({"host": HOST, "camera": CAMERA, "serial": SERIAL, "bench_dir": BENCH_DIR}[sys.argv[1]])
