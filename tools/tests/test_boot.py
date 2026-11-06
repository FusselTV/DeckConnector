"""Boot and connection times after a power cut, as regression tests. A hardware reset over the serial bridge
stands in for the power cut, so these run only on the bench: `pytest -m boot`.

The bridge records each milestone itself (`boot_ms` in /api/status, ms since the app started; the bootloader
before that takes about 1.3 s). Budgets are about 1.5 times what the bench measured.
"""

import json
import os
import subprocess
import time
from pathlib import Path

import pytest
from conftest import api

pytestmark = pytest.mark.boot

TOOLS = Path(__file__).resolve().parents[1]
BUDGET_MS = {
    "deck": 1500,
    "lan_link": 4000,
    "wlan_ip": 9000,
    "lan_ip": 11000,
    "companion": 13000,
    "host_draws": 13000,
    "app": 16000,
}


def power_cut() -> None:
    """A reset through the serial bridge, keeping /tmp/esp-live.log as tools/flash.sh does. With
    DECKBRIDGE_POWER_CUT=poe a real one over PoE instead, with DECKBRIDGE_USB_UNPLUGGED=1 (poe-test.md)."""
    if os.environ.get("DECKBRIDGE_POWER_CUT") == "poe":
        from dbtools.poe import power_cut as poe_cut

        poe_cut()
        return
    subprocess.run(["pkill", "-f", "[d]btools[.]serlog"], check=False)
    time.sleep(1)
    with open("/tmp/esp-live.log", "w") as log:
        subprocess.Popen(
            ["uv", "run", "--quiet", "python", "-m", "dbtools.serlog", "--reset", "--secs", "86400"],
            cwd=TOOLS,
            stdout=log,
            stderr=subprocess.STDOUT,
            start_new_session=True,
        )


def boot_marks(wait_for: set, secs: float = 45) -> dict:
    end = time.time() + secs
    marks = {}
    while time.time() < end:
        try:
            marks = json.loads(api("GET", "/api/status", timeout=2)[1])["boot_ms"]
            if wait_for <= marks.keys():
                return marks
        except OSError:
            pass
        time.sleep(0.5)
    return marks


@pytest.mark.parametrize("run", range(3))
def test_power_cut_to_the_hosts_picture(run):
    time.sleep(8)  # the hosts of the run before may still be connecting
    status = json.loads(api("GET", "/api/status")[1])
    assert status["controller"] == "companion", "the budgets are for Companion drawing"
    # a host counts when it was there before the cut: it must come back
    there = {x["kind"] for x in status["sources"] if x["connected"]}
    wanted = {k: v for k, v in BUDGET_MS.items() if k not in ("companion", "app") or k in there}
    power_cut()
    time.sleep(3)
    marks = boot_marks(set(wanted))
    print(f"\nrun {run}: " + ", ".join(f"{k} {marks.get(k, '-')}" for k in BUDGET_MS))
    late = {k: marks.get(k) for k, budget in wanted.items() if not marks.get(k) or marks[k] > budget}
    assert not late, f"over budget or missing: {late}"
