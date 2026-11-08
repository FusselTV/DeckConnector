"""The web page from the www partition: served at once, kept over a restart, refused when it is not ours."""

import gzip
import json
import time
import urllib.request

import pytest
from conftest import HOST, api, status, wait_for_bridge
from cryptography.hazmat.primitives import serialization

from dbtools import dbfw

KEY = serialization.load_pem_private_key(dbfw.KEY.read_bytes(), None)


def page() -> bytes:
    with urllib.request.urlopen(f"http://{HOST}/", timeout=10) as r:
        return gzip.decompress(r.read())


def image(marker: str, api_min: int | None = None, target: str = "web") -> bytes:
    payload = gzip.compress(dbfw.PAGE.read_bytes().replace(b"</body>", f"<!--{marker}--></body>".encode()))
    return dbfw.build(target, payload, f"web {marker}", KEY, dbfw.web_api() if api_min is None else api_min)


def put(img: bytes) -> tuple[int, dict]:
    code, body = api("PUT", "/api/frontend", img)
    return code, json.loads(body)


@pytest.mark.sim
def test_the_page_in_use_and_the_api_version():
    info = json.loads(api("GET", "/api/frontend")[1])
    assert info["source"] in ("builtin", "partition") and info["api"] == dbfw.web_api() == status()["api"]


def test_a_page_in_the_partition_is_served_and_bad_ones_are_refused():
    started = status()["uptime_s"]
    try:
        code, info = put(image("first"))
        assert (code, info["source"], info["version"]) == (200, "partition", "web first")
        assert b"<!--first-->" in page(), "the new page is served without a restart"

        good = image("second")
        bad_sig = bytearray(good)
        bad_sig[80] ^= 1
        bad_data = bytearray(good)
        bad_data[-1] ^= 1
        refused = {
            "firmware image": image("p4", target="p4"),
            "bad signature": bytes(bad_sig),
            "changed page": bytes(bad_data),
            "needs a newer API": image("future", api_min=dbfw.web_api() + 1),
        }
        for why, img in refused.items():
            code, body = put(img)
            assert code == 400, f"{why}: {code} {body}"
        assert b"<!--first-->" in page(), "a refused upload keeps the page before"
        assert status()["uptime_s"] >= started, "no restart"

        api("POST", "/api/reboot")
        time.sleep(3)
        wait_for_bridge(60)
        assert b"<!--first-->" in page(), "the page is read from the partition at start"
    finally:
        code, body = api("DELETE", "/api/frontend")
    assert (code, json.loads(body)["source"]) == (200, "builtin")
    assert b"<!--first-->" not in page()
