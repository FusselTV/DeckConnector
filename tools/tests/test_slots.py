"""Firmware slots and the boot regions as the API reports them. Nothing here restarts or writes."""

import hashlib
import json

import pytest
from conftest import api


def slots() -> list[dict]:
    return json.loads(api("GET", "/api/slots")[1])


def test_one_running_slot_and_it_is_valid():
    s = slots()
    assert len(s) == 4 and [x["slot"] for x in s] == [0, 1, 2, 3]
    running = [x for x in s if x["running"]]
    assert len(running) == 1 and running[0]["state"] == "valid" and running[0]["version"]


def test_a_slot_that_cannot_start_is_refused():
    assert api("POST", "/api/slots/9/boot")[0] == 404
    empty = [x["slot"] for x in slots() if x["state"] in ("empty", "invalid")]
    if empty:
        assert api("POST", f"/api/slots/{empty[0]}/boot")[0] == 409


@pytest.mark.parametrize("region", ["bootloader", "ptable"])
def test_a_region_download_matches_its_hash(region):
    code, raw = api("GET", f"/api/boot?raw={region}")
    assert code == 200 and raw
    assert hashlib.sha256(raw).hexdigest() == json.loads(api("GET", "/api/boot")[1])[region]["sha256"]
