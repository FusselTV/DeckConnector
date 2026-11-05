"""Page switching through the host that draws (Companion, the Elgato app): turn the page encoder fast and see how
long the keys keep changing after the last turn. Turns back as far afterwards, so the page is where it was.

Only the keys count: a clock on the touch strip or on a key redraws every second, so the end of a page switch is
the last key change before a pause of PAUSE_S.

uv run python -m dbtools.pagebench [--encoder 3] [--gap-ms 25]
"""

import argparse
import base64
import json
import os
import time
from pathlib import Path

from dbtools.load import Client

PAUSE_S = 0.4
WATCH_S = 2.5


def post(c: Client, path: str, body: dict) -> None:
    if not c.conn:
        c.get("/api/status")  # opens the keep-alive connection
    c.conn.request("POST", path, body=json.dumps(body), headers={"Authorization": c.auth, "X-Deckbridge": "1"})
    c.conn.getresponse().read()


def keys(c: Client) -> tuple:
    sc = json.loads(c.get("/api/screen")[2])
    rects = {(k["x"], k["y"], k["w"], k["h"]) for k in sc["keys"]}
    return tuple(i["url"] for i in sc["images"] if (i["x"], i["y"], i["w"], i["h"]) in rects)


def run(c: Client, encoder: int, turns: int, gap_ms: int, ticks: int) -> int:
    """Milliseconds from the last turn to the last key change of the switch."""
    time.sleep(1.2)  # let the page before settle
    for _ in range(turns):
        post(c, "/api/input", {"encoder": encoder, "action": "turn", "ticks": ticks})
        time.sleep(gap_ms / 1000)
    last_turn = time.time() - gap_ms / 1000
    seen, changes = keys(c), []
    while time.time() - last_turn < WATCH_S:
        now = keys(c)
        if now != seen:
            changes.append(time.time())
            seen = now
    end = last_turn
    for t in changes:
        if t - end > PAUSE_S:
            break
        end = t
    return round((end - last_turn) * 1000)


def main() -> None:
    ap = argparse.ArgumentParser()
    ap.add_argument("--host", default=os.environ.get("DB_HOST", "192.168.1.214"))
    ap.add_argument("--encoder", type=int, default=3)
    ap.add_argument("--gap-ms", type=int, default=25)
    ap.add_argument("--rounds", type=int, default=5)
    a = ap.parse_args()
    pw = (Path.home() / ".config/deckbridge/admin_pass").read_text().strip()
    c = Client(a.host, "Basic " + base64.b64encode(f"admin:{pw}".encode()).decode())
    print(f"writer {json.loads(c.get('/api/status')[2])['writer']}, encoder {a.encoder + 1}, {a.gap_ms} ms apart")
    for turns in (1, 5, 10, 20):
        late = []
        for _ in range(a.rounds):
            late.append(run(c, a.encoder, turns, a.gap_ms, 1))
            run(c, a.encoder, turns, 150, -1)  # back to where it was, slowly
        late.sort()
        print(f"{turns:2} turns: keys done {late[len(late) // 2]:4} ms after the last turn (worst {late[-1]} ms)")


if __name__ == "__main__":
    main()
