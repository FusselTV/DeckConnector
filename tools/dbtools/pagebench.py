"""Page switching through the host that draws, which must be a test Companion on this machine (dbtools.safety): turn
the page encoder fast and see how long the keys keep changing after the last turn. Turns back as far afterwards, so
the page is where it was.

Only the keys count: a clock on the touch strip or on a key redraws every second, so the end of a page switch is
the last key change before a pause of PAUSE_S.

uv run python -m dbtools.pagebench [--encoder 3] [--gap-ms 25]
"""

import argparse
import json
import time

from dbtools.load import HOST, Client, basic_auth
from dbtools.safety import real_input_target

PAUSE_S = 0.4
WATCH_S = 2.5


def keys(c: Client) -> tuple:
    sc = json.loads(c.get("/api/screen")[2])
    rects = {(k["x"], k["y"], k["w"], k["h"]) for k in sc["keys"]}
    return tuple(i["url"] for i in sc["images"] if (i["x"], i["y"], i["w"], i["h"]) in rects)


def run(c: Client, encoder: int, turns: int, gap_ms: int, ticks: int, target: str) -> int:
    """Milliseconds from the last turn to the last key change of the switch."""
    time.sleep(1.2)  # let the page before settle
    for _ in range(turns):
        c.send("POST", "/api/input", {"encoder": encoder, "action": "turn", "ticks": ticks, "real": target})
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
    ap.add_argument("--host", default=HOST)
    ap.add_argument("--encoder", type=int, default=3)
    ap.add_argument("--gap-ms", type=int, default=25)
    ap.add_argument("--rounds", type=int, default=5)
    a = ap.parse_args()
    c = Client(a.host, basic_auth())
    status = json.loads(c.get("/api/status")[2])
    target = real_input_target(status)  # refuses unless a host on this machine draws; the bridge checks each turn
    print(f"writer {status['writer']}, encoder {a.encoder + 1}, {a.gap_ms} ms apart")
    for turns in (1, 5, 10, 20):
        late = []
        for _ in range(a.rounds):
            late.append(run(c, a.encoder, turns, a.gap_ms, 1, target))
            run(c, a.encoder, turns, 150, -1, target)  # back to where it was, slowly
        late.sort()
        print(f"{turns:2} turns: keys done {late[len(late) // 2]:4} ms after the last turn (worst {late[-1]} ms)")


if __name__ == "__main__":
    main()
