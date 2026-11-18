"""The bridge refuses settings that would leave it unreachable or nameless, with an error that says why."""

import json

import pytest
from conftest import api


def put(body: dict) -> tuple[int, str]:
    code, reply = api("PUT", "/api/config", json.dumps(body).encode())
    return code, json.loads(reply).get("error", "")


@pytest.mark.sim
@pytest.mark.parametrize(
    ("body", "word"),
    [
        ({"name": ""}, "name"),
        ({"name": "my deck"}, "name"),
        ({"name": "-deck"}, "name"),
        ({"name": "déck"}, "name"),
        ({"name": "x" * 32}, "name"),
        ({"admin_pass": "x" * 24}, "6 to 23"),
        ({"admin_pass": "short"}, "6 to 23"),
        ({"wifi_pass": "1234567"}, "wifi_pass"),
        ({"wifi_pass": "x" * 64}, "wifi_pass"),
        ({"wifi2_pass": "1234567"}, "wifi2_pass"),
        ({"wifi2_ssid": "x" * 33}, "wifi2_ssid"),
        ({"servers": ["wss://h /x"]}, "servers"),
        ({"servers": ["http://h:1/x"]}, "servers"),
        ({"satellite": "h /x"}, "satellite"),
        ({"lan": {"dhcp": False, "ip": "10.0.0.5", "mask": "0.0.0.0"}}, "mask"),
        ({"lan": {"dhcp": False, "ip": "10.0.0.5", "mask": "255.0.255.0"}}, "mask"),
        ({"lan": {"dhcp": False, "ip": "10.0.0.5", "mask": "255.255.255.255"}}, "mask"),
        ({"lan": {"dhcp": False, "ip": "10.0.0.0", "mask": "255.255.255.0"}}, "ip"),
        ({"lan": {"dhcp": False, "ip": "10.0.0.255", "mask": "255.255.255.0"}}, "ip"),
        ({"lan": {"dhcp": False, "ip": "10.0.0.5", "mask": "255.255.255.0", "gateway": "10.0.1.1"}}, "gateway"),
        ({"lan": {"dhcp": False, "ip": "10.0.0.5", "mask": "255.255.255.0", "gateway": "10.0.0.5"}}, "gateway"),
    ],
)
def test_a_bad_setting_is_refused_with_its_reason(body, word):
    code, error = put(body)
    assert code == 400 and word in error, error


@pytest.mark.sim
def test_the_stored_name_still_passes():
    assert put({"name": json.loads(api("GET", "/api/config")[1])["name"]})[0] == 200  # unchanged: no restart


@pytest.mark.sim
def test_a_pairing_answer_names_the_host():
    assert api("POST", "/api/dock", b'{"accept": true}')[0] == 409
    assert api("POST", "/api/dock", b'{"accept": true, "ip": "192.0.2.1"}')[0] == 409
