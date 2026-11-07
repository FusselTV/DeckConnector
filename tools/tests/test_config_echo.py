"""A settings change answers with what the bridge stored, so the page cannot claim more than it knows."""

import json

import pytest
from conftest import api


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
