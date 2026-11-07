"""Simulated presses reach Companion and the Stream Deck app only with "real". `true` is for a person at the web UI.
Tools name the host instead, from dbtools.safety.real_input_target, which allows only a host on this machine, and
the bridge forwards a press only while that host draws (see dbtools/safety.py)."""

import re
from ipaddress import ip_address
from pathlib import Path

import pytest

from dbtools.safety import NOBODY, UnsafeBridge, real_input_target

pytestmark = pytest.mark.sim

TOOLS = Path(__file__).resolve().parents[1]
# "real": x (dict or JSON), ["real"] = x, and real=x as a keyword
BOUND = re.compile(r"""["']real["']\s*:\s*(\S+)|\[\s*["']real["']\s*\]\s*=(?!=)\s*(\S+)|[(,]\s*real\s*=(?!=)\s*(\S+)""")


def sources():
    for pattern in ("*.py", "*.mjs", "*.sh"):
        for f in TOOLS.rglob(pattern):
            if not {".venv", "node_modules"} & set(f.parts) and f.name not in ("safety.py", Path(__file__).name):
                yield f, f.read_text()


def nobody(value: str) -> bool:
    literal = re.match(r"""["']([^"']*)["']""", value)
    try:
        return bool(literal) and ip_address(literal[1]) in NOBODY
    except ValueError:
        return False


def test_nothing_sends_real_presses_without_the_check():
    offenders = []
    for f, text in sources():
        for m in BOUND.finditer(text):
            value = next(v for v in m.groups() if v)
            if nobody(value):
                continue
            # a literal address can only be someone's real host; anything else must come from the check
            if re.match(r"""["']""", value) or "real_input_target(" not in text:
                offenders.append(f"{f.relative_to(TOOLS)}: {m[0]}")
    assert not offenders, f"sends real presses without dbtools.safety.real_input_target: {offenders}"


def test_the_scan_sees_every_form():
    for text in ('{"real": "192.168.1.15"}', 'b["real"] = me', "dict(key=1, real=me)", "x = {'real': host}"):
        assert BOUND.search(text), text
    assert not BOUND.search("real = json.loads(body)")
    assert nobody('"192.0.2.1"') and not nobody('"192.168.1.15"') and not nobody("me")


def test_tools_never_send_real_true():
    # as a dict literal, a keyword (dict(real=True), json(real=...)) or JSON text
    true = re.compile(r"""["']real["']\s*:\s*(True|true)\b|\breal\s*=\s*True\b""")
    offenders = [
        str(f.relative_to(TOOLS))
        for f, text in sources()
        if f.name != "conftest.py" and true.search(text)  # the guard itself
    ]
    assert not offenders, f"tools name the host, true is for a person at the web UI: {offenders}"


@pytest.mark.parametrize("host", ["companion.local:16622", "ws://studio.lan:8765/x", "192.168.1.15"])
def test_only_this_machine_by_address_is_a_real_target(host):
    status = {"simulated_input": "local", "sources": [{"drawing": True, "host": host, "kind": "companion"}]}
    with pytest.raises(UnsafeBridge):
        real_input_target(status)
