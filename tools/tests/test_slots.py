"""Firmware slots and the boot regions as the API reports them. Nothing here restarts or writes."""

import hashlib
import json

import pytest
from conftest import api


def slots() -> list[dict]:
    return json.loads(api("GET", "/api/slots")[1])


def test_one_running_slot_and_it_is_valid():
    s = slots()
    assert len(s) == 4 and [x["slot"] for x in s] == [1, 2, 3, 4]
    running = [x for x in s if x["running"]]
    assert len(running) == 1 and running[0]["state"] == "valid" and running[0]["version"]


def test_a_slot_that_cannot_start_is_refused():
    assert api("POST", "/api/slots/9/boot")[0] == 404
    s = slots()
    empty = [x["slot"] for x in s if x["state"] in ("empty", "invalid")]
    if empty:
        code, reply = api("POST", f"/api/slots/{empty[0]}/boot")
        assert code == 409
        assert json.loads(reply)["error"] == s[empty[0] - 1]["why_not"], "the list says why before you try"
    for x in s:
        assert x["bootable"] is (not x["running"] and "why_not" not in x)


def test_slot_zero_is_refused_not_taken_as_the_next_slot():
    """API 1 clients counted from 0; their slot 0 must not quietly become "the slot after the running one"."""
    assert json.loads(api("GET", "/api/status")[1])["api"] >= 2
    assert api("POST", "/api/ota?slot=0", b"tiny")[0] == 400


@pytest.mark.parametrize("region", ["bootloader", "ptable"])
def test_a_region_download_matches_its_hash(region):
    code, raw = api("GET", f"/api/boot?raw={region}")
    assert code == 200 and raw
    assert hashlib.sha256(raw).hexdigest() == json.loads(api("GET", "/api/boot")[1])[region]["sha256"]
