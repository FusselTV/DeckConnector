"""The log of the boot before this one, the reason for the restart, and the NTP server setting."""

import contextlib
import json
import time
import urllib.request

import pytest
from conftest import HOST, TARGET, api, status, wait_for_bridge


@pytest.mark.sim
def test_the_status_names_why_the_bridge_started():
    boot = status()["boot"]
    assert boot["reason"] in {"power", "pin", "restart", "crash", "watchdog", "brownout", "other"}, boot
    assert boot["at"] is None or boot["at"] > 1700000000


@pytest.mark.sim
@pytest.mark.parametrize("server", ["a b", 'x"y', "http://pool.ntp.org"])
def test_a_bad_ntp_server_is_refused(server):
    code, body = api("PUT", "/api/config", json.dumps({"ntp_server": server}).encode())
    assert code == 400 and b"ntp_server" in body, body


@pytest.mark.skipif(TARGET == "sim", reason="the simulator does not restart")
def test_the_log_before_a_restart_stays_readable():
    wrong = urllib.request.Request(f"http://{HOST}/api/config", headers={"Authorization": "Basic d3Jvbmc6eA=="})
    with contextlib.suppress(urllib.error.HTTPError):
        urllib.request.urlopen(wrong, timeout=5)  # logs "wrong web password", the last line before the restart
    api("POST", "/api/reboot")
    time.sleep(5)
    wait_for_bridge(60)
    assert status()["boot"]["reason"] == "restart"
    code, prev = api("GET", "/api/log?boot=prev")
    assert code == 200 and b"wrong web password" in prev, prev[-300:]
    assert status()["boot"]["at"], "SNTP set no clock"
