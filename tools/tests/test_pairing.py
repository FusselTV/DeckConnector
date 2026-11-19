"""A program that is not paired is turned away and asks on the deck; accepted it gets in, refused it does not."""

import json
import socket
import time
import warnings

import pytest
from conftest import HOST, api, get_config, status


def dock(body: dict) -> None:
    assert api("POST", "/api/dock", json.dumps(body).encode())[0] == 200


def admitted() -> bool:
    """A connection to the dock port that the bridge keeps open was let in; a refused one is closed at once."""
    s = socket.create_connection((HOST, 5343), timeout=3)
    s.settimeout(1.0)
    try:
        return s.recv(1) != b""  # the dock sends a keepalive within 2.5 s, a refused socket ends at once
    except TimeoutError:
        return True
    except ConnectionResetError:
        return False
    finally:
        s.close()


def known() -> list[str]:
    return [h["ip"] for h in status()["pairing"]["hosts"]]


def strict(on: bool) -> None:
    assert api("PUT", "/api/config", json.dumps({"dock_paired_only": on}).encode())[0] == 200


def test_open_by_default_and_hosts_listed_with_mac():
    p = status()["pairing"]
    assert p["only_paired"] is False and p["request"] is None
    assert admitted()
    me = socket.create_connection((HOST, 80), timeout=3).getsockname()[0]
    host = next(h for h in status()["pairing"]["hosts"] if h["ip"] == me)
    assert len(host["mac"]) == 17, "a host on the same network is known by its MAC, so a new DHCP address is no issue"
    assert status()["pairing"]["auto_open"] is False, "a host is listed: the next one needs pairing"


def test_pairing():
    me = socket.create_connection((HOST, 80), timeout=3).getsockname()[0]
    if not [ip for ip in known() if ip != me]:
        pytest.skip("with nothing paired the dock lets everyone in, by design")
    before = get_config()["dock_paired_only"]
    strict(True)
    dock({"pairing": 0})
    dock({"forget": me})
    try:
        assert not admitted()
        p = status()["pairing"]
        assert p["request"] == me and me not in known()
        assert p["request_name"].lower() == socket.gethostbyaddr(me)[0].split(".")[0].lower(), (
            "named as the router knows it"
        )
        assert status()["writer"] == "ui", "the deck shows the request"
        assert api("POST", "/api/dock", b'{"accept": false}')[0] == 409, "an answer names the host"
        assert api("POST", "/api/dock", b'{"accept": false, "ip": "192.0.2.1"}')[0] == 409
        dock({"accept": False, "ip": me})
        p = status()["pairing"]
        assert p["request"] is None
        assert [d["ip"] for d in p["denied"]][-1] == me, "listed as refused"
        assert not admitted(), "refused stays out"
        assert status()["pairing"]["request"] is None, "a refused host is not asked about again for a minute"
        assert api("DELETE", f"/api/pairing/denied/{me}")[0] == 200
        assert not admitted()

        # the request page's second key accepts, but only for a person at the deck: a simulated press leaves it
        assert status()["pairing"]["request"] == me
        assert api("POST", "/api/input", b'{"key": 1}')[0] == 200
        time.sleep(0.5)
        if me in known():  # firmware before the owner's decision of 2026-11-07
            warnings.warn("this firmware lets a simulated press admit a new dock host", stacklevel=1)
        else:
            assert status()["pairing"]["request"] == me, "a simulated press must leave the request pending"
            dock({"accept": True, "ip": me})
            assert me in known()
        assert me not in [d["ip"] for d in status()["pairing"]["denied"]], "accepted: off the refused list"
        assert api("DELETE", f"/api/pairing/denied/{me}")[0] == 404
        assert admitted()
    finally:
        api("POST", "/api/dock", json.dumps({"accept": False, "ip": me}).encode())  # 409 when nothing asks
        strict(before)
