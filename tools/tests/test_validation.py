"""The bridge refuses settings that would leave it unreachable or nameless, with an error that says why."""

import json
import struct
import subprocess
import urllib.error
import urllib.request

import pytest
from conftest import WORKER, api, start_sim
from test_evlog import NEW_TABLE, idf_python, table, write_at


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
        ({"brightness": 50.5}, "brightness"),
        ({"volume": 10.5}, "volume"),
        ({"menu_hold_ms": 1500.5}, "menu_hold_ms"),
        ({"max_decks": 1.5}, "max_decks"),
        ({"screen_off_s": 60.5}, "screen_off_s"),
        ({"screen_dim": 30.5}, "screen_dim"),
        ({"lan": {"dhcp": False, "ip": "10.0.0.08", "mask": "255.255.255.0"}}, "IPv4"),
        ({"wlan": {"dhcp": False, "ip": "10.0.0.5", "mask": "255.255.255.0", "gateway": "10.0.0.010"}}, "IPv4"),
        ({"lan": {"dhcp": True, "dns": " 1.1.1.1"}}, "IPv4"),
        ({"lan": {"dhcp": True, "dns": "+1.1.1.1"}}, "IPv4"),
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


def ip_blob(dhcp: bool, *fields: str) -> bytes:
    return struct.pack("<?16s16s16s16s", dhcp, *(f.encode() for f in (*fields, "", "", "", "")[:4]))


@pytest.mark.sim
def test_a_stored_address_now_refused_becomes_the_one_applied(tmp_path):
    """Older firmware saved addresses with a leading zero; kept, every later save of the network page would fail."""
    head = struct.pack("<I32siB", 5, b"sim-old", 1, 80)  # db_config_t up to lan, version 5
    (tmp_path / "c.bin").write_bytes(
        head
        + ip_blob(False, "10.0.0.09", "255.255.255.0")
        + ip_blob(False, "10.0.0.010", "255.255.255.0", "", "01.1.1.1")
    )
    (tmp_path / "nvs.csv").write_text(
        f"key,type,encoding,value\ndeckbridge,namespace,,\nconfig,file,binary,{tmp_path / 'c.bin'}\n"
    )
    gen = [
        idf_python(),
        "-m",
        "esp_idf_nvs_partition_gen",
        "generate",
        tmp_path / "nvs.csv",
        tmp_path / "nvs.bin",
        "0x6000",
    ]
    subprocess.run(gen, check=True, capture_output=True)
    flash = tmp_path / "flash.bin"
    flash.write_bytes(b"\xff" * 0x90000)
    write_at(flash, 0x8000, table(NEW_TABLE, tmp_path))
    write_at(flash, 0x9000, (tmp_path / "nvs.bin").read_bytes())
    port = 38280 + WORKER
    sim = start_sim(port, DB_SIM_PASSWORD="simulator-pass", DB_SIM_FLASH=str(flash))
    try:

        def call(method: str, body: bytes | None = None) -> tuple[int, dict]:
            req = urllib.request.Request(f"http://127.0.0.1:{port}/api/config", body, method=method)
            req.add_header("Authorization", "Basic YWRtaW46c2ltdWxhdG9yLXBhc3M=")
            req.add_header("X-DeckConnector", "1")
            try:
                with urllib.request.urlopen(req, timeout=5) as r:
                    return r.status, json.loads(r.read())
            except urllib.error.HTTPError as e:
                return e.code, json.loads(e.read())

        cfg = call("GET")[1]
        assert cfg["name"] == "sim-old", "the stored settings were loaded"
        assert cfg["lan"]["dhcp"] and cfg["lan"]["ip"] == "", "lwip applied no address from 09: DHCP"
        assert (cfg["wlan"]["dhcp"], cfg["wlan"]["ip"], cfg["wlan"]["dns"]) == (False, "10.0.0.8", "1.1.1.1"), (
            "as lwip read them"
        )
        code, reply = call("PUT", json.dumps({"lan": cfg["lan"], "wlan": cfg["wlan"]}).encode())
        assert code == 200, reply
    finally:
        sim.kill()
        sim.wait()
