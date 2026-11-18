"""A settings change answers with what the bridge stored, so the page cannot claim more than it knows."""

import json
import time

import pytest
from conftest import HOST, TARGET, api, status, wait_for_bridge


@pytest.mark.sim
def test_a_change_answers_with_the_stored_settings():
    before = json.loads(api("GET", "/api/config")[1])["menu_hold_ms"]
    try:
        code, body = api("PUT", "/api/config", json.dumps({"menu_hold_ms": 2500}).encode())
        reply = json.loads(body)
        assert code == 200 and reply["restart"] is False and reply["config"]["menu_hold_ms"] == 2500, body
        assert "admin_pass" not in reply["config"]
    finally:
        api("PUT", "/api/config", json.dumps({"menu_hold_ms": before}).encode())


@pytest.mark.sim
def test_volume_works_at_once():
    before = json.loads(api("GET", "/api/config")[1])["volume"]
    try:
        code, body = api("PUT", "/api/config", json.dumps({"volume": 40 if before != 40 else 41}).encode())
        assert code == 200 and json.loads(body)["restart"] is False, body
    finally:
        api("PUT", "/api/config", json.dumps({"volume": before}).encode())


@pytest.mark.sim
def test_the_status_carries_the_settings_home_assistant_sets():
    config = json.loads(api("GET", "/api/config")[1])
    try:
        api("PUT", "/api/config", json.dumps({"screen_off_s": 600, "volume": 33}).encode())
        st = status()
        assert (st["screen_off_s"], st["volume"], st["max_decks"]) == (600, 33, config["max_decks"]), st
    finally:
        api("PUT", "/api/config", json.dumps({k: config[k] for k in ("screen_off_s", "volume")}).encode())


@pytest.mark.skipif(TARGET == "sim", reason="the simulator ends on a restart")
def test_a_restart_names_where_the_page_finds_the_bridge():
    name = json.loads(api("GET", "/api/config")[1])["name"]
    try:
        reply = json.loads(api("PUT", "/api/config", b'{"ntp_server": "pool.ntp.org"}')[1])
        assert reply["restart"] is True and f"http://{name}.local" in reply["reach"], reply
        assert f"http://{HOST}" in reply["reach"], reply
    finally:
        time.sleep(5)
        wait_for_bridge(60)
        api("PUT", "/api/config", b'{"ntp_server": ""}')
        time.sleep(5)
        wait_for_bridge(60)
