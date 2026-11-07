"""The input log keeps the last 200 inputs with their origin, and never more."""

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
def test_the_log_never_holds_more_than_200():
    for _ in range(110):  # 220 events: a press is down and up
        api("POST", "/api/input", json.dumps({"key": 1, "real": "192.0.2.1"}).encode())
    log = json.loads(api("GET", "/api/input/log")[1])
    assert len(log) == 200 and log[-1]["seq"] - log[0]["seq"] == 199
