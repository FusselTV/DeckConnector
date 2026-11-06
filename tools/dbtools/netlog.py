"""Follow the bridge's log over the network, from the oldest line it still keeps (the boot, after a restart).

uv run python -m dbtools.netlog [--secs 60] [--host 192.168.1.214]
Needs the password (~/.config/deckbridge/admin_pass): a token gets no log.
"""

import argparse
import base64
import json
import os
import sys
import time
from pathlib import Path

from websockets.sync.client import connect


def follow(host: str, password: str, secs: float, out=sys.stdout) -> None:
    auth = "Basic " + base64.b64encode(f"admin:{password}".encode()).decode()
    end = time.time() + secs
    with connect(f"ws://{host}/api/ws", additional_headers={"Authorization": auth}, open_timeout=10) as ws:
        ws.send(json.dumps({"t": "log", "id": 1}))
        while time.time() < end:
            try:
                msg = json.loads(ws.recv(timeout=max(0.1, end - time.time())))
            except TimeoutError:
                break
            if msg.get("t") == "log":
                out.write(msg["text"])
                out.flush()
            elif msg.get("t") == "result" and msg.get("status") != 200:
                raise SystemExit(f"log refused: {msg}")


def main() -> None:
    ap = argparse.ArgumentParser()
    ap.add_argument("--host", default=os.environ.get("DB_HOST", "192.168.1.214"))
    ap.add_argument("--secs", type=float, default=10)
    a = ap.parse_args()
    follow(a.host, (Path.home() / ".config/deckbridge/admin_pass").read_text().strip(), a.secs)


if __name__ == "__main__":
    main()
