"""What a client on the network puts on the deck: after a No there, the same address waits a minute."""

import http.client
import json
import socket
import time

from conftest import dismiss_prompt, drawn_since

YES, NO = 1, 2


def ask(sim, source: str) -> tuple[int, dict]:
    """A login request from `source`, past the 3 s between two requests of anyone."""
    for _ in range(5):
        c = http.client.HTTPConnection("127.0.0.1", sim.port, timeout=10, source_address=(source, 0))
        c.request("POST", "/api/login/request", body=b'{"who": "simdeck"}', headers={"X-DeckConnector": "1"})
        r = c.getresponse()
        reply = json.loads(r.read())
        if r.status != 429 or reply.get("code") == "paused":
            return r.status, reply
        time.sleep(1)
    raise AssertionError("still 429 after 5 s")


def answer(sim, key: int, rid: str) -> str:
    assert sim.wait(lambda: sim.status()["menu_open"], 5), "the deck does not ask"
    time.sleep(0.3)
    sim.tap(0, key)
    c = http.client.HTTPConnection("127.0.0.1", sim.port, timeout=10)
    state = lambda: (c.request("GET", f"/api/login/request/{rid}"), json.loads(c.getresponse().read()))[1]["state"]  # noqa: E731
    return sim.wait(lambda: (s := state()) != "pending" and s, 5)


def test_a_no_on_the_deck_holds_off_that_address_and_a_yes_does_not(start):
    sim = start("plus")
    dismiss_prompt(sim)
    sim.config(setup_done=True)
    code, reply = ask(sim, "127.0.0.2")
    assert code == 200, reply
    assert answer(sim, NO, reply["id"]) == "denied"
    code, reply = ask(sim, "127.0.0.2")
    assert (code, reply.get("code")) == (429, "paused") and 58 <= reply["retry"] <= 60, reply
    code, reply = ask(sim, "127.0.0.3")
    assert code == 200, f"another address may still ask: {reply}"
    assert answer(sim, YES, reply["id"]) == "accepted"
    assert ask(sim, "127.0.0.3")[0] == 200, "a Yes holds nobody off"


def post(sim, path: str, source: str) -> tuple[int, dict]:
    c = http.client.HTTPConnection("127.0.0.1", sim.port, timeout=10, source_address=(source, 0))
    c.request("POST", path, body=b"", headers={"X-DeckConnector": "1"})
    r = c.getresponse()
    return r.status, json.loads(r.read())


def test_hide_on_the_password_page_and_cancel_on_the_reset_page_hold_off_that_address(start):
    sim = start("plus")
    dismiss_prompt(sim)
    sim.config(setup_done=True)
    for path, key in (("/api/password/show", 0), ("/api/password/reset", 3)):  # any key hides; Cancel is the 4th
        assert post(sim, path, "127.0.0.6")[0] == 200
        assert sim.wait(lambda: sim.status()["menu_open"], 5), f"{path}: the deck shows nothing"
        time.sleep(0.3)
        sim.tap(0, key)
        assert sim.wait(lambda: not sim.status()["menu_open"], 5), f"{path}: the page stays"
        code, reply = post(sim, path, "127.0.0.6")
        assert (code, reply.get("code")) == (429, "paused"), (path, reply)


def dock_from(source: str) -> None:
    s = socket.create_connection(("127.0.0.1", 5343), timeout=3, source_address=(source, 0))
    time.sleep(0.3)
    s.close()


def test_a_dock_host_refused_on_the_deck_asks_again_only_a_minute_later(start):
    sim = start("plus")
    dismiss_prompt(sim)
    sim.config(setup_done=True, dock_paired_only=True)
    dock_from("127.0.0.2")  # the first host gets in while none is known
    assert sim.api("POST", "/api/dock", {"pairing": 0})[0] == 200
    since = time.time()
    dock_from("127.0.0.4")
    assert sim.wait(lambda: sim.status()["pairing"]["request"] == "127.0.0.4", 5), sim.status()["pairing"]
    assert sim.wait(lambda: drawn_since(sim, 0, since), 5), "the deck does not ask"
    time.sleep(0.3)
    sim.tap(0, NO)
    assert sim.wait(lambda: sim.status()["pairing"]["request"] is None, 5)
    dock_from("127.0.0.4")
    time.sleep(0.5)
    assert sim.status()["pairing"]["request"] is None, "a refused host covers the deck again at once"
    dock_from("127.0.0.5")
    assert sim.wait(lambda: sim.status()["pairing"]["request"] == "127.0.0.5", 5), "another host may still ask"
