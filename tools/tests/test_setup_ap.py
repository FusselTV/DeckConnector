"""A client on the setup access point needs no password; one on the LAN or the WLAN still does."""

import json
import os
import subprocess
import time
import urllib.request
from pathlib import Path

import pytest
from conftest import HOST, SIM, TARGET, api, status


def call(host: str, method: str, path: str, marked: bool = True) -> tuple[int, bytes]:
    headers = {"X-DeckConnector": "1"} if marked else {}
    req = urllib.request.Request(f"http://{host}{path}", data=None, method=method, headers=headers)
    try:
        with urllib.request.urlopen(req, timeout=10) as r:
            return r.status, r.read()
    except urllib.error.HTTPError as e:
        return e.code, e.read()


@pytest.mark.sim
def test_the_lan_still_needs_the_password():
    assert status()["via_ap"] is False
    assert call(HOST, "GET", "/api/status")[0] == 401
    assert call(HOST, "GET", "/api/tokens")[0] == 401
    assert api("DELETE", "/api/tokens/none", auth=False)[0] == 401


@pytest.fixture
def ap_sim():
    """A second simulator whose setup AP has the loopback address: every request comes over the AP."""
    worker = int(os.environ.get("PYTEST_XDIST_WORKER", "gw0")[2:])
    port = 18081 + 2 * worker  # between the workers' ports
    log = Path(f"/tmp/deckbridge-sim-ap-{port}.log")
    sim = subprocess.Popen(
        [SIM],
        env={**os.environ, "DB_SIM_PORT": str(port), "DB_SIM_VIA_AP": "1"},
        stdout=log.open("w"),
        stderr=subprocess.STDOUT,
    )
    try:
        for _ in range(100):
            try:
                urllib.request.urlopen(f"http://127.0.0.1:{port}/", timeout=1)
                break
            except OSError:
                time.sleep(0.05)
        yield f"127.0.0.1:{port}"
    finally:
        sim.kill()
        sim.wait()


@pytest.mark.sim
@pytest.mark.skipif(TARGET != "sim", reason="this machine cannot join the bridge's setup AP")
def test_the_setup_ap_needs_no_password(ap_sim):
    code, body = call(ap_sim, "GET", "/api/status", marked=False)
    assert code == 200 and json.loads(body)["via_ap"] is True
    assert call(ap_sim, "GET", "/api/tokens", marked=False)[0] == 200  # the password's own routes too
    assert call(ap_sim, "DELETE", "/api/tokens/none")[0] == 404  # past the login, no such token


@pytest.mark.sim
@pytest.mark.skipif(TARGET != "sim", reason="this machine cannot join the bridge's setup AP")
def test_the_setup_ap_still_needs_the_marker_for_changes(ap_sim):
    assert call(ap_sim, "DELETE", "/api/tokens/none", marked=False)[0] == 401
