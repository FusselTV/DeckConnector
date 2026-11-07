"""With input_lock on, the web API takes no press and no source switch unless the request says "force"."""

import json

import pytest
from conftest import api, put_config


def post(path, body):
    return api("POST" if path == "/api/input" else "PUT", path, json.dumps(body).encode())[0]


@pytest.mark.sim
def test_a_locked_bridge_refuses_presses_and_switches_without_force():
    cfg = json.loads(api("GET", "/api/config")[1])
    other = "app" if cfg["controller"] != "app" else "companion"
    assert post("/api/config", {"input_lock": True}) == 200
    try:
        assert post("/api/input", {"key": 0}) == 423
        assert post("/api/config", {"controller": other}) == 423  # refused: production stays as it is
        assert post("/api/input", {"key": 0, "real": "192.0.2.1", "force": True}) == 200
    finally:
        put_config({"input_lock": False})
    assert json.loads(api("GET", "/api/config")[1])["controller"] == cfg["controller"]


@pytest.mark.sim
def test_arming_opens_the_lock_for_a_while():
    assert post("/api/config", {"input_lock": True}) == 200
    try:
        assert json.loads(api("PUT", "/api/input/arm", b'{"s": 30}')[1]) == {"armed_s": 30}
        assert 0 < json.loads(api("GET", "/api/status")[1])["input_armed_s"] <= 30
        assert post("/api/input", {"key": 0, "real": "192.0.2.1"}) == 200
        assert post("/api/input/arm", {"s": 0}) == 200
        assert post("/api/input", {"key": 0, "real": "192.0.2.1"}) == 423
        assert post("/api/input/arm", {"s": 3601}) == 400
    finally:
        put_config({"input_lock": False})
    assert json.loads(api("GET", "/api/status")[1])["input_armed_s"] == 0
