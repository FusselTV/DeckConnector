"""Updates pushed by a server. Slow: each one restarts the bridge. Run with `pytest -m ota`."""

import asyncio
import functools
import http.server
import threading
from pathlib import Path

import pytest
from conftest import wait_for_bridge
from cryptography.hazmat.primitives import serialization

from dbtools import dbfw
from dbtools.server import local_ip

pytestmark = pytest.mark.ota
ROOT = Path(__file__).resolve().parents[2]
C6_IMAGE = Path.home() / "projects/streamdeck/c6-cp/build/eh_cp_ota_coprocessor_ota.bin"


@pytest.fixture(scope="module")
def files(tmp_path_factory):
    """Serve signed images over HTTP like a server would."""
    root = tmp_path_factory.mktemp("fw")
    key = serialization.load_pem_private_key(dbfw.KEY.read_bytes(), None)
    p4 = (ROOT / "firmware/build/deckbridge.bin").read_bytes()
    # esp_app_desc_t follows the 24 byte image header and an 8 byte segment header; version is at +16.
    version = p4[48:80].split(b"\0")[0].decode()
    (root / "p4.dbfw").write_bytes(dbfw.build("p4", p4, version, key))
    if C6_IMAGE.exists():
        (root / "c6.dbfw").write_bytes(dbfw.build("c6", C6_IMAGE.read_bytes(), "c6", key))
    handler = functools.partial(http.server.SimpleHTTPRequestHandler, directory=root)
    httpd = http.server.ThreadingHTTPServer(("0.0.0.0", 8008), handler)
    threading.Thread(target=httpd.serve_forever, daemon=True).start()
    yield f"http://{local_ip()}:8008", version
    httpd.shutdown()


async def push(bridges, url: str, target: str) -> list[dict]:
    active, _ = bridges
    await active.send_json({"t": "ota", "target": target, "url": url})
    for _ in range(600):
        if active.ota and active.ota[-1]["state"] in ("done", "error"):
            break
        await asyncio.sleep(0.1)
    states = list(active.ota)
    active.ota.clear()
    await asyncio.to_thread(wait_for_bridge, 90)
    return states


async def test_p4_update_from_url(servers, bridges, files):
    base, version = files
    states = await push(bridges, f"{base}/p4.dbfw", "p4")
    assert states[-1]["state"] == "done", states
    assert [s["state"] for s in states].count("download") >= 5
    first, _ = servers
    back = await first.wait_bridge(90)
    assert back.hello["fw"] == version


async def test_missing_file_reports_an_error(servers, files):
    base, _ = files
    first, _ = servers
    bridge = next(iter(first.bridges.values()))
    await bridge.send_json({"t": "ota", "target": "p4", "url": f"{base}/missing.dbfw"})
    for _ in range(100):
        if bridge.ota:
            break
        await asyncio.sleep(0.1)
    assert bridge.ota and bridge.ota[-1]["state"] == "error"
    bridge.ota.clear()


@pytest.mark.skipif(not C6_IMAGE.exists(), reason="no C6 image built")
async def test_c6_update_from_url(servers, files):
    base, _ = files
    first, _ = servers
    bridge = next(iter(first.bridges.values()))
    await bridge.send_json({"t": "ota", "target": "c6", "url": f"{base}/c6.dbfw"})
    for _ in range(1200):
        if bridge.ota and bridge.ota[-1]["state"] in ("done", "error"):
            break
        await asyncio.sleep(0.1)
    assert bridge.ota[-1]["state"] == "done", bridge.ota
    back = await first.wait_bridge(120)
    assert back.hello["c6"] == "3.0.10"
