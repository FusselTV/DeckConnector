"""Simulated presses reach Companion and the Stream Deck app only with "real": true. Nothing in tools/ may send it
except through dbtools.safety.real_input_target, which allows only a host on this machine (see dbtools/safety.py)."""

import re
from pathlib import Path

TOOLS = Path(__file__).resolve().parents[1]


def test_nothing_sends_real_presses_without_the_check():
    real = re.compile(r"""["']real["']\s*:""")
    offenders = []
    for f in TOOLS.rglob("*.py"):
        if ".venv" in f.parts or f.name in ("safety.py", Path(__file__).name):
            continue
        text = f.read_text()
        if real.search(text) and "real_input_target(" not in text:
            offenders.append(str(f.relative_to(TOOLS)))
    assert not offenders, f"sends real presses without dbtools.safety.real_input_target: {offenders}"
