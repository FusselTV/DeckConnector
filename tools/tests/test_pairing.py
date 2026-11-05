"""A program that is not paired is turned away and asks on the deck; accepted it gets in, refused it does not."""

import json
import socket
import time

from conftest import HOST, api, status


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


def test_pairing():
    me = socket.create_connection((HOST, 80), timeout=3).getsockname()[0]
    before = status()["pairing"]["known"]
    if not [ip for ip in before if ip != me]:
        return  # with nothing paired the dock lets everyone in, by design
    dock({"pairing": 0})
    dock({"forget": me})
    try:
        assert not admitted()
        p = status()["pairing"]
        assert p["request"] == me and me not in p["known"]
        assert status()["writer"] == "ui", "the deck shows the request"
        dock({"accept": False})
        assert status()["pairing"]["request"] is None
        assert not admitted(), "refused stays out"

        # on the deck: the request page, its second key accepts
        assert status()["pairing"]["request"] == me
        assert api("POST", "/api/input", b'{"key": 1}')[0] == 200
        time.sleep(0.5)
        assert me in status()["pairing"]["known"]
        assert admitted()
    finally:
        dock({"forget": me})
        dock({"accept": False})
