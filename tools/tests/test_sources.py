"""Connections the bridge makes report why they are down and can be retried at once."""

import json

import pytest
from conftest import TARGET, api, status


@pytest.mark.sim
def test_a_retry_for_an_unknown_host_is_a_404():
    assert api("POST", "/api/sources/retry", json.dumps({"host": "ws://nowhere:1/x"}).encode())[0] == 404


@pytest.mark.skipif(TARGET == "sim", reason="needs the Satellite client")
def test_the_satellite_source_reports_its_health_and_takes_a_retry():
    sat = next(s for s in status()["sources"] if s.get("via") == "satellite")
    assert {"error", "since_s", "iface"} <= sat.keys()
    if sat["connected"]:
        assert sat["error"] == "" and sat["iface"] in ("lan", "wlan") and 0 <= sat["since_s"] <= status()["uptime_s"]
    host = json.loads(api("GET", "/api/config")[1])["satellite"]
    assert api("POST", "/api/sources/retry", json.dumps({"host": host}).encode())[0] == 200
