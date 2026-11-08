"""Improv Wi-Fi Serial against firmware/sim, over its stdin and stdout: the settings go through the same path as
the web UI. The bench bridge's serial console is not used: these commands change settings."""

import base64
import json
import os
import subprocess
import time
import urllib.request

import pytest
from conftest import SIM, WORKER

from dbtools import improv as im

pytestmark = [pytest.mark.sim, pytest.mark.skipif(not SIM.exists(), reason="firmware/sim is not built")]


class Sim:
    def __init__(self, tmp_path, **env: str) -> None:
        self.port = 18300 + 2 * WORKER
        self.p = subprocess.Popen(
            [SIM],
            cwd=tmp_path,
            env={**os.environ, "DB_SIM_PORT": str(self.port), "DB_SIM_PASSWORD": "simulator", **env},
            stdin=subprocess.PIPE,
            stdout=subprocess.PIPE,
            stderr=subprocess.STDOUT,
        )
        os.set_blocking(self.p.stdout.fileno(), False)
        self.reader = im.Reader()
        for _ in range(100):
            try:
                urllib.request.urlopen(f"http://127.0.0.1:{self.port}/", timeout=1)
                return
            except OSError:
                time.sleep(0.05)
        raise RuntimeError("the simulator did not start")

    def write(self, b: bytes) -> None:
        self.p.stdin.write(b)
        self.p.stdin.flush()

    def read(self, n: int) -> bytes:
        time.sleep(0.02)
        return self.p.stdout.read(n) or b""

    def ask(self, packet: bytes, until=None, cmd: int | None = None) -> list[tuple[int, bytes]]:
        return im.ask(self, self.reader, packet, until or im.result_for(cmd))

    def result(self, cmd: int, *strings: str) -> list[str]:
        got = self.ask(im.rpc(cmd, *strings), cmd=cmd)
        results = [im.strings(d[2:]) for k, d in got if k == im.RESULT and d[0] == cmd]
        assert results, got
        return results[-1]

    def error(self, cmd: int, *strings: str) -> int:
        got = self.ask(im.rpc(cmd, *strings), cmd=cmd)
        errors = [d[0] for k, d in got if k == im.ERROR and d[0]]
        assert errors, got
        return errors[-1]

    def config(self, password: str) -> dict:
        auth = base64.b64encode(f"admin:{password}".encode()).decode()
        req = urllib.request.Request(
            f"http://127.0.0.1:{self.port}/api/config", headers={"Authorization": f"Basic {auth}"}
        )
        return json.load(urllib.request.urlopen(req, timeout=2))


@pytest.fixture
def sim(tmp_path):
    s = Sim(tmp_path)
    yield s
    s.p.kill()


def test_state_info_and_network(sim):
    got = sim.ask(im.rpc(im.CURRENT_STATE), until=im.state_done)
    assert (im.STATE, bytes([4])) in got  # the simulated LAN is up
    assert [im.strings(d[2:]) for k, d in got if k == im.RESULT] == [["http://127.0.0.1"]]
    info = sim.result(im.INFO)
    assert info[0] == "DeckConnector" and info[2] == "ESP32-P4" and info[3].startswith("deckconnector-")
    assert sim.result(im.NETWORK) == ["5", "http://127.0.0.1"]  # online, Ethernet
    assert sim.result(im.NAME) == [info[3]]
    assert sim.result(im.HOSTNAME) == [info[3]]


def test_scan_ends_with_an_empty_result(sim):
    got = sim.ask(im.rpc(im.SCAN), until=lambda got: any(k == im.RESULT and d[1] == 0 for k, d in got))
    nets = [im.strings(d[2:]) for k, d in got if k == im.RESULT and d[1]]
    assert nets[0] == ["Zuhause", "-48", "YES"] and nets[-1] == ["Gast", "-70", "NO"]


def test_wifi_join(sim):
    got = sim.ask(im.rpc(im.WIFI, "Zuhause", "password1"), cmd=im.WIFI)
    assert (im.STATE, bytes([3])) in got and (im.STATE, bytes([4])) in got
    assert sim.error(im.WIFI, "Elsewhere", "password1") == 3
    assert sim.error(im.WIFI, "Zuhause", "short") == 1


def test_unknown_and_broken_packets(sim):
    assert sim.error(0x42) == 2
    broken = bytearray(im.rpc(im.INFO))
    broken[-2] ^= 1  # checksum
    assert any(k == im.ERROR and d[0] == 1 for k, d in sim.ask(bytes(broken), cmd=im.INFO))
    assert sim.result(im.INFO)[0] == "DeckConnector"  # and a good one still works


def test_setup_follows_the_web_rules(sim):
    assert sim.result(im.SETUP, "abc", "", "") == ["error", "admin_pass: 6 to 23 characters"]
    assert sim.result(im.SETUP, "", "-bad", "")[0] == "error"
    assert sim.error(im.NAME, "bad name") == 5
    assert sim.result(im.SETUP, "", "", "") == ["ok"]  # nothing to change
    assert sim.result(im.SETUP, "", "", "1") == ["ok"]
    assert sim.config("simulator")["setup_done"] is True


def test_setup_saves_password_and_name(sim):
    assert sim.result(im.SETUP, "improv-pass", "improv-test", "") == ["restart"]
    cfg = sim.config("improv-pass")  # before the restart one second later ends the simulator
    assert cfg["name"] == "improv-test" and cfg["setup_done"] is False


def test_settings_close_after_setup_and_the_window(tmp_path):
    s = Sim(tmp_path, DB_SIM_IMPROV_OPEN_S="0")
    try:
        assert s.result(im.SETUP, "", "", "1") == ["ok"]  # setup not done: open
        assert s.error(im.NAME, "other-name") == 4
        assert s.error(im.WIFI, "Zuhause", "password1") == 4
        assert s.error(im.SETUP, "new-password", "", "") == 4
        assert s.error(im.SCAN) == 4
        assert s.result(im.INFO)[0] == "DeckConnector"  # reading stays open
        assert s.config("simulator")["name"].startswith("deckconnector-")
    finally:
        s.p.kill()
