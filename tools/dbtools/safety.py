"""Simulated presses must never reach a host whose buttons switch real devices.

On 2026-11-06 a test sent 150 presses while Companion drew, and Home Assistant switched lights, scenes and a vacuum
519 times in 40 s. Firmware that reports `simulated_input: "local"` keeps simulated input away from Companion, the
Elgato App and Satellite unless the request carries "real": true. Every test and tool that presses checks
`require_input_gate` first, and sends "real" only through `real_input_target`.
"""

import ipaddress
from urllib.parse import urlparse

from dbtools.server import local_ip


class UnsafeBridge(RuntimeError):
    pass


# RFC 5737 documentation addresses: no host has one, so a press bound to it can reach nobody.
NOBODY = ipaddress.ip_network("192.0.2.0/24")


def require_input_gate(status: dict) -> None:
    if status.get("simulated_input") != "local":
        raise UnsafeBridge(
            f"firmware {status.get('firmware')} passes simulated presses on to Companion and the Elgato App. "
            "Deploy firmware that reports simulated_input=local before pressing anything."
        )


def real_input_target(status: dict) -> str:
    """The address of the drawing host, if "real" presses may go to it: only a host on this test machine."""
    require_input_gate(status)
    drawing = [s for s in status["sources"] if s.get("drawing")]
    if len(drawing) != 1:
        raise UnsafeBridge(f"no single drawing host: {drawing}")
    host = drawing[0]["host"]
    addr = urlparse(host).hostname if "://" in host else host.split(":")[0]
    if not addr or ipaddress.ip_address(addr) != ipaddress.ip_address(local_ip()):
        raise UnsafeBridge(f"{drawing[0]['kind']} at {host} draws: real presses go only to a host on this machine")
    return addr


# CLAUDE.md: the bench is the owner's deck
PRODUCTION = {"controller": "companion", "host": "satellite", "satellite": "wss://companion.example.com/satellite"}


def settings_to_restore(cfg: dict) -> dict:
    """What a run sets back: what it found, unless that is a test controller a killed run left behind."""
    if cfg["controller"] == "server":
        return dict(PRODUCTION)
    return {k: cfg.get(k, "") for k in PRODUCTION}
