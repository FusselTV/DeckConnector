"""Networks in reach and joining one: tried first, kept only when it gives an address."""

import json
import time

import pytest
from conftest import TARGET, api, status


@pytest.mark.sim
def test_the_scan_lists_access_points_with_signal_and_channel():
    code, body = api("GET", "/api/wifi/scan", timeout=15)
    aps = json.loads(body)
    assert code == 200 and aps, body
    for a in aps:
        assert a["ssid"] and len(a["bssid"]) == 17 and -100 < a["rssi"] < 0 and 1 <= a["channel"] <= 14
    setup_ap = status().get("network", {}).get("ap", {}).get("ssid")  # the simulator has no network block
    assert not setup_ap or setup_ap not in {a["ssid"] for a in aps}


@pytest.mark.sim
@pytest.mark.parametrize(
    "body",
    [{}, {"ssid": ""}, {"ssid": "x" * 33}, {"ssid": "a", "pass": "p" * 65}, {"ssid": "a", "bssid": "nope"}],
)
def test_a_bad_join_request_is_refused(body):
    assert api("POST", "/api/wifi/join", json.dumps(body).encode())[0] == 400


@pytest.mark.sim
def test_a_bad_bssid_lock_is_refused():
    assert api("PUT", "/api/config", json.dumps({"wifi_bssid": "zz:00:00:00:00:00"}).encode())[0] == 400


def wait_join(secs: float) -> dict:
    end = time.time() + secs
    while time.time() < end:
        wlan = status()["network"]["wlan"]
        if wlan["join"] != "testing":
            return wlan
        time.sleep(0.5)
    return wlan


@pytest.mark.skipif(TARGET == "sim", reason="needs the radio")
def test_a_network_that_is_not_there_fails_and_the_old_one_comes_back():
    before = status()["network"]["wlan"]
    assert api("POST", "/api/wifi/join", json.dumps({"ssid": "no-such-net-x7", "pass": "12345678"}).encode())[0] == 202
    wlan = wait_join(25)
    assert wlan["join"] == "failed" and wlan["join_error"], wlan
    assert json.loads(api("GET", "/api/config")[1])["wifi_ssid"] == before["ssid"]
    end = time.time() + 30
    while before["ip"] and time.time() < end and status()["network"]["wlan"]["ip"] != before["ip"]:
        time.sleep(1)
    assert status()["network"]["wlan"]["ip"] == before["ip"]
