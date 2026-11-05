"""The API answers over both networks, in about the time the bench measured."""

import base64
import json
import statistics
import time
import urllib.request

import pytest
from conftest import HOST, PASSWORD, api


def timed_status(ip: str, n: int = 30) -> float:
    token = base64.b64encode(f"admin:{PASSWORD}".encode()).decode()
    times = []
    for _ in range(n):
        req = urllib.request.Request(f"http://{ip}/api/status", headers={"Authorization": f"Basic {token}"})
        t = time.perf_counter()
        with urllib.request.urlopen(req, timeout=5) as r:
            r.read()
        times.append(time.perf_counter() - t)
    return statistics.median(times) * 1000


@pytest.mark.parametrize("net, budget_ms", [("lan", 15), ("wlan", 80)])
def test_api_over(net, budget_ms):
    ip = json.loads(api("GET", "/api/status")[1])["network"][net]["ip"]
    if not ip:
        pytest.skip(f"no {net} address")
    if net == "lan":
        assert ip == HOST
    took = timed_status(ip)
    print(f"{net} {ip}: median {took:.1f} ms")
    assert took < budget_ms
