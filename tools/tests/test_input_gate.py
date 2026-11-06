"""Simulated presses stay away from Companion and the Stream Deck app unless "real" is set (dbtools/safety.py).

A dock host on this machine (companion-check, the library inside Companion) draws for the test, so no real
Companion is ever the target, whatever the firmware does.
"""

import json
import re
import subprocess
import time
from pathlib import Path

from conftest import HOST, api, status

from dbtools.safety import real_input_target
from dbtools.server import local_ip

CHECK = Path(__file__).resolve().parents[1] / "companion-check"


def test_only_real_presses_reach_a_dock_host():
    me = local_ip()
    cfg = json.loads(api("GET", "/api/config")[1])
    host = subprocess.Popen(["node", "check.mjs", HOST, "8"], cwd=CHECK, stdout=subprocess.PIPE, text=True)
    try:
        for line in host.stdout:
            if line.startswith("listening"):
                break
        api("PUT", "/api/config", json.dumps({"controller": "companion", "host": me}).encode())
        for _ in range(20):
            try:
                real_input_target(status())
                break
            except Exception:
                time.sleep(0.5)
        target = real_input_target(status())  # never press while anyone else's host draws
        for key in (0, 1, 2, 3):
            assert api("POST", "/api/input", json.dumps({"key": key}).encode())[0] == 200
        # bound to another address, which does not draw: the bridge keeps it, though this host draws
        assert api("POST", "/api/input", json.dumps({"key": 5, "real": "192.0.2.1"}).encode())[0] == 200
        assert api("POST", "/api/input", json.dumps({"key": 4, "real": target}).encode())[0] == 200
        out = host.communicate(timeout=30)[0]
    finally:
        host.kill()
        api("PUT", "/api/config", json.dumps({"controller": cfg["controller"], "host": cfg["host"]}).encode())
    seen = json.loads(re.search(r"^input (.*)$", out, re.M)[1])
    assert seen.get("button-down") == 1, f"the dock host saw {seen}, expected only the one real press"
