"""The event history across restarts and partition tables, on firmware/sim with a flash image that outlives it.

A table without `evlog` keeps the behaviour from before it, the last 40 events in the settings storage. Once the
table has `evlog`, the next start moves them into it, and those from a firmware before event codes come along as text.
"""

import glob
import json
import os
import struct
import subprocess
import sys
import time
import urllib.request
from pathlib import Path

import pytest
from conftest import WORKER, start_sim

pytestmark = pytest.mark.sim

ROOT = Path(__file__).resolve().parents[2]
IDF = Path(os.environ.get("IDF_PATH", Path.home() / "esp/esp-idf"))
OLD_TABLE = "nvs, data, nvs, 0x9000, 0x6000\n"
NEW_TABLE = (ROOT / "firmware/sim/partitions.csv").read_text()
PORT = 38180 + WORKER
AUTH = "Basic YWRtaW46c2ltdWxhdG9yLXBhc3M="  # admin:simulator-pass
LONG = "WLAN roam Fussel-IoT 7c:10:c9:aa:01:01 -> 7c:10:c9:aa:01:02, channel 11"


def idf_python() -> str:
    env = os.environ.get("IDF_PYTHON_ENV_PATH")
    found = [f"{env}/bin/python"] if env else sorted(glob.glob(str(Path.home() / ".espressif/python_env/*/bin/python")))
    if not found:
        pytest.skip("needs the ESP-IDF Python environment for its NVS generator")
    return found[-1]


def table(csv: str, tmp: Path) -> bytes:
    (tmp / "t.csv").write_text(csv)
    script = IDF / "components/partition_table/gen_esp32part.py"
    subprocess.run([sys.executable, script, "-q", tmp / "t.csv", tmp / "t.bin"], check=True)
    return (tmp / "t.bin").read_bytes()


def legacy_nvs(tmp: Path) -> bytes:
    """The settings storage as a firmware before event codes left it: boot count 5, four events in "evlog"."""
    rows = ["key,type,encoding,value", "evlog,namespace,,", "boot,data,u32,5"]
    old = [("boot", "started (power)"), ("lan", "LAN link down"), ("wlan", LONG), ("source", "Companion lost: closed")]
    for i, (kind, text) in enumerate(old):
        blob = struct.pack("<qIII12s84s", 1763500000 + i, 10 + i, 7 + i, 4, kind.encode(), text.encode())
        (tmp / f"e{i}.bin").write_bytes(blob.ljust(120, b"\0"))  # legacy_t is 120 bytes with its padding
        rows.append(f"e{(7 + i) % 40},file,binary,{tmp / f'e{i}.bin'}")
    (tmp / "nvs.csv").write_text("\n".join(rows) + "\n")
    gen = [idf_python(), "-m", "esp_idf_nvs_partition_gen", "generate", tmp / "nvs.csv", tmp / "nvs.bin", "0x6000"]
    subprocess.run(gen, check=True, capture_output=True)
    return (tmp / "nvs.bin").read_bytes()


def write_at(path: Path, offset: int, data: bytes) -> None:
    with path.open("r+b") as f:
        f.seek(offset)
        f.write(data)


def boot(flash: Path) -> list[dict]:
    """Starts the simulator on the image, waits until its first events are stored, and gives what it reports."""
    sim = start_sim(PORT, DB_SIM_PASSWORD="simulator-pass", DB_SIM_FLASH=str(flash))
    try:
        req = urllib.request.Request(
            f"http://127.0.0.1:{PORT}/api/events",
            headers={"Authorization": AUTH, "X-DeckConnector": "1"},
        )
        body = json.loads(urllib.request.urlopen(req, timeout=5).read())
        time.sleep(1)  # the keeper stores this boot's start right after it begins
        return [body]
    finally:
        sim.kill()
        sim.wait()


def texts(body: dict) -> list[str]:
    return [e["text"] for e in reversed(body["events"])]


def test_an_old_table_keeps_events_in_settings_and_evlog_takes_them_over(tmp_path):
    flash = tmp_path / "flash.bin"
    flash.write_bytes(b"\xff" * 0x90000)
    write_at(flash, 0x8000, table(OLD_TABLE, tmp_path))
    write_at(flash, 0x9000, legacy_nvs(tmp_path))

    first = boot(flash)[0]
    assert first["store"] == "settings", "without evlog the settings storage keeps the events, as today"
    assert texts(first)[:4] == ["started (power)", "LAN link down", LONG, "Companion lost: closed"]
    assert [e["boot"] for e in reversed(first["events"])][:4] == [2, 2, 2, 2], "boot 4 of a count now at 6"
    assert first["events"][0]["code"] == "started" and first["events"][0]["boot"] == 0
    assert all(e["code"] == "text" for e in first["events"][1:])

    second = boot(flash)[0]
    assert second["store"] == "settings"
    assert texts(second)[:5] == texts(first), "the events of the boot before, as today"

    write_at(flash, 0x8000, table(NEW_TABLE, tmp_path))  # what the signed ptable update writes
    third = boot(flash)[0]
    assert third["store"] == "flash"
    assert texts(third)[:6] == texts(second), "the settings storage's events moved into evlog, in order"
    assert third["events"][0]["boot"] == 0 and third["events"][1]["boot"] == 1

    fourth = boot(flash)[0]
    assert fourth["store"] == "flash"
    assert texts(fourth)[:7] == texts(third), "kept in evlog, and moved only once"
    seqs = [e["seq"] for e in fourth["events"]]
    assert seqs == sorted(seqs, reverse=True) and len(set(seqs)) == len(seqs)


def test_pages_go_back_by_number_and_stop_at_a_time(tmp_path):
    flash = tmp_path / "flash.bin"
    flash.write_bytes(b"\xff" * 0x90000)
    write_at(flash, 0x8000, table(NEW_TABLE, tmp_path))
    write_at(flash, 0x9000, legacy_nvs(tmp_path))
    sim = start_sim(PORT, DB_SIM_PASSWORD="simulator-pass", DB_SIM_FLASH=str(flash))
    try:

        def get(q: str) -> dict:
            url = f"http://127.0.0.1:{PORT}/api/events{q}"
            req = urllib.request.Request(url, headers={"Authorization": AUTH, "X-DeckConnector": "1"})
            return json.loads(urllib.request.urlopen(req, timeout=5).read())

        everything = get("")
        seqs = [e["seq"] for e in everything["events"]]
        assert len(seqs) == 5 and everything["store"] == "flash", "4 moved, the long one in two records, 1 new"
        assert [e["seq"] for e in get("?n=2")["events"]] == seqs[:2]
        assert [e["seq"] for e in get(f"?before={seqs[1]}&n=2")["events"]] == seqs[2:4]
        assert get(f"?before={seqs[-1]}")["events"] == []
        newest_at = everything["events"][0]["at"]
        assert [e["seq"] for e in get(f"?since={newest_at}")["events"]] == [seqs[0]], "older events end the page"
    finally:
        sim.kill()
        sim.wait()


def test_a_new_event_goes_out_on_the_web_socket():
    from websockets.sync.client import connect

    sim = start_sim(PORT, DB_SIM_PASSWORD="simulator-pass")
    try:
        auth = {"Authorization": AUTH}
        with connect(f"ws://127.0.0.1:{PORT}/api/ws", additional_headers=auth) as ws:
            req = urllib.request.Request(
                f"http://127.0.0.1:{PORT}/api/config",
                data=json.dumps({"host": "192.0.2.5", "port": 16622}).encode(),
                headers={"Content-Type": "application/json"},
                method="POST",
            )
            urllib.request.urlopen(req, timeout=5).read()  # Companion's Setup: logged as an event
            end = time.time() + 5
            while time.time() < end:
                msg = json.loads(ws.recv(timeout=max(0.1, end - time.time())))
                if msg["t"] == "event":
                    break
            else:
                raise AssertionError("no event on the socket")
        assert msg["event"]["code"] in ("companion_setup", "companion_asks")
        assert msg["event"]["args"] == ["192.0.2.5:16622"]
        assert msg["event"]["text"].startswith("Companion 192.0.2.5:16622")
    finally:
        sim.kill()
        sim.wait()
