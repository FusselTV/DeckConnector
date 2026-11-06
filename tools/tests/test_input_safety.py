"""Simulated presses reach Companion and the Stream Deck app only with "real". `true` is for a person at the web UI.
Tools name the host instead, from dbtools.safety.real_input_target, which allows only a host on this machine, and
the bridge forwards a press only while that host draws (see dbtools/safety.py)."""

import re
from ipaddress import ip_address
from pathlib import Path

import pytest

from dbtools.safety import NOBODY

pytestmark = pytest.mark.sim

TOOLS = Path(__file__).resolve().parents[1]


def test_nothing_sends_real_presses_without_the_check():
    real = re.compile(r"""["']real["']\s*:""")
    offenders = []
    for f in TOOLS.rglob("*.py"):
        if ".venv" in f.parts or f.name in ("safety.py", Path(__file__).name):
            continue
        text = f.read_text()
        bound = re.findall(r"""["']real["']\s*:\s*["']([^"']*)["']""", text)
        if all(ip_address(a) in NOBODY for a in bound) and len(bound) == len(real.findall(text)):
            continue  # every press is bound to an address no host has
        if real.search(text) and "real_input_target(" not in text:
            offenders.append(str(f.relative_to(TOOLS)))
    assert not offenders, f"sends real presses without dbtools.safety.real_input_target: {offenders}"


def test_tools_never_send_real_true():
    # as a dict literal, a keyword (dict(real=True), json(real=...)) or JSON text
    true = re.compile(r"""["']real["']\s*:\s*(True|true)\b|\breal\s*=\s*True\b""")
    offenders = [
        str(f.relative_to(TOOLS))
        for f in TOOLS.rglob("*.py")
        if ".venv" not in f.parts
        and f.name not in (Path(__file__).name, "safety.py", "conftest.py")  # the guards themselves
        and true.search(f.read_text())
    ]
    assert not offenders, f"tools name the host, true is for a person at the web UI: {offenders}"
