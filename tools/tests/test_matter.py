"""Matter is off on a new board, its switch outlives a restart, and a factory reset turns it off again."""

import json
import time
import urllib.error
import urllib.request
from pathlib import Path

import pytest
from conftest import HOST, TARGET, WORKER, api, start_sim, status, wait_for_bridge
from test_discover import ours
from test_evlog import NEW_TABLE, table, write_at
from zeroconf import Zeroconf

PORT = 38380 + WORKER


def call(method: str, path: str, body: dict | None = None) -> tuple[int, dict]:
    data = None if body is None else json.dumps(body).encode()
    req = urllib.request.Request(f"http://127.0.0.1:{PORT}{path}", data, method=method)
    req.add_header("Authorization", "Basic YWRtaW46c2ltdWxhdG9yLXBhc3M=")
    req.add_header("X-DeckConnector", "1")
    try:
        with urllib.request.urlopen(req, timeout=5) as r:
            return r.status, json.loads(r.read())
    except urllib.error.HTTPError as e:
        return e.code, json.loads(e.read())


@pytest.mark.sim
def test_switch_survives_a_restart_and_a_factory_reset_turns_it_off(tmp_path: Path) -> None:
    flash = tmp_path / "flash.bin"
    flash.write_bytes(b"\xff" * 0x90000)
    write_at(flash, 0x8000, table(NEW_TABLE, tmp_path))
    sim = start_sim(PORT, DB_SIM_PASSWORD="simulator-pass", DB_SIM_FLASH=str(flash))

    def restart() -> None:
        nonlocal sim
        sim.kill()
        sim.wait()
        sim = start_sim(PORT, DB_SIM_PASSWORD="simulator-pass", DB_SIM_FLASH=str(flash))

    def matter() -> dict:
        return call("GET", "/api/status")[1]["matter"]

    try:
        assert matter() == {"on": False, "running": False, "controllers": 0, "pairing": False}
        assert call("GET", "/api/matter")[0] == 409
        assert call("POST", "/api/matter", {"pair": True})[0] == 409

        assert call("POST", "/api/matter", {"on": True}) == (200, {"ok": True})
        assert matter()["running"], "on starts at once"
        restart()
        assert matter()["running"], "and stays on after a restart"
        code, codes = call("GET", "/api/matter")
        assert code == 200 and codes["code"] and codes["qr_bits"].startswith("21:")

        assert call("POST", "/api/matter", {"on": False})[0] == 200
        assert matter() == {"on": False, "running": True, "controllers": 0, "pairing": True}, "off needs a restart"
        restart()
        assert not matter()["running"]

        assert call("POST", "/api/matter", {"on": True})[0] == 200
        assert call("POST", "/api/reset")[0] == 200
        restart()
        assert not matter()["on"], "a factory reset turns Matter off"
    finally:
        sim.kill()
        sim.wait()


@pytest.mark.sim
def test_the_backup_carries_the_switch(tmp_path: Path) -> None:
    flash = tmp_path / "flash.bin"
    flash.write_bytes(b"\xff" * 0x90000)
    write_at(flash, 0x8000, table(NEW_TABLE, tmp_path))
    sim = start_sim(PORT, DB_SIM_PASSWORD="simulator-pass", DB_SIM_FLASH=str(flash))
    try:
        assert call("GET", "/api/backup?secrets=1")[1]["matter"] is False
        assert call("PUT", "/api/config", {"matter": "yes"})[0] == 400
        code, reply = call("PUT", "/api/config", {"matter": True})
        assert code == 200 and reply["restart"] is False and reply["config"]["matter"] is True, reply
        assert call("GET", "/api/status")[1]["matter"]["running"], "on starts at once, as POST /api/matter"
        assert call("GET", "/api/backup?secrets=1")[1]["matter"] is True
        code, reply = call("PUT", "/api/config", {"matter": False})
        assert code == 200 and reply["restart"] is True, "off stops Matter only with a restart"
    finally:
        sim.kill()
        sim.wait()


@pytest.mark.skipif(TARGET == "sim", reason="the simulator has no Matter stack")
def test_turned_on_without_a_restart_it_can_be_paired():
    """Both LAN and WLAN up when Matter starts: its mDNS restarts on the new addresses must not run out of sockets."""
    now = status()
    if now["matter"]["on"] or not now["network"]["wlan"]["ip"]:
        pytest.skip("needs a bridge with Matter off and the WLAN joined")
    try:
        assert api("POST", "/api/matter", b'{"on": true}')[0] == 200
        assert api("POST", "/api/matter", b'{"pair": true}')[0] == 200  # a paired board opens no window by itself
        time.sleep(15)  # past the new addresses and the mDNS restarts after them: an early announcement proves nothing
        zc = Zeroconf()
        try:
            assert ours(zc, "_matterc._udp.local."), f"no _matterc._udp from {HOST}"
        finally:
            zc.close()
    finally:
        try:
            assert api("POST", "/api/matter", b'{"on": false}')[0] == 200
        finally:
            api("POST", "/api/reboot")
            time.sleep(5)
        assert not wait_for_bridge(60)["matter"]["running"], "Matter still on after the restart"
