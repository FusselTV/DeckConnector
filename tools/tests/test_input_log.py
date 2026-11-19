"""The input log keeps the last 2000 inputs with their origin, and never more; it pages them 400 at a time."""

import json

import pytest
from conftest import api


@pytest.mark.sim
def test_inputs_are_logged_with_origin_and_since_filters():
    before = json.loads(api("GET", "/api/input/log")[1])
    last = before[-1]["seq"] if before else 0
    # bound to an address nobody has: logged, but no host can get it
    assert api("POST", "/api/input", json.dumps({"key": 0, "real": "192.0.2.1"}).encode())[0] == 200
    new = json.loads(api("GET", f"/api/input/log?since={last}")[1])
    assert [e["kind"] for e in new] == ["key_down", "key_up"] and all(e["origin"] == "tool" for e in new), new
    assert new[0]["seq"] == last + 1


@pytest.mark.sim
def test_the_log_keeps_2000_in_pages_of_400_newest_first():
    for _ in range(1100):  # 2200 inputs: a press is down and up
        api("POST", "/api/input", json.dumps({"key": 1, "real": "192.0.2.1"}).encode())
    page = json.loads(api("GET", "/api/input/log")[1])
    newest = page[-1]["seq"]
    assert len(page) == 400 and newest - page[0]["seq"] == 399, "the newest page, oldest first"
    kept = page
    while len(page) == 400:
        page = json.loads(api("GET", f"/api/input/log?before={kept[0]['seq']}")[1])
        kept = page + kept
    assert len(kept) == 2000 and [e["seq"] for e in kept] == list(range(newest - 1999, newest + 1))
