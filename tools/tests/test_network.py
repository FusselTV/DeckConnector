"""The API answers over both networks, in about the time the bench measured."""

import base64
import json
import random
import socket
import statistics
import struct
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


def test_a_client_port_the_bridge_closed_connects_again():
    """The bridge holds a port it closed in TIME-WAIT and ignores the client's reset there. A client that picks
    the same port again must not wait out the whole TIME-WAIT."""

    def connect(port):
        s = socket.socket()
        s.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
        s.bind(("0.0.0.0", port))
        s.settimeout(3)
        s.connect((HOST, 80))
        return s

    base = random.randint(61000, 65000)  # above the ephemeral range, so no other socket holds them
    idle = [connect(base + i) for i in range(14)]  # more than the server's 12 sessions: it closes the oldest
    time.sleep(2)
    closed = []
    for i, s in enumerate(idle):
        s.settimeout(0.2)
        try:
            if s.recv(10) == b"":
                closed.append(base + i)
        except ConnectionResetError:
            closed.append(base + i)  # a reset closes it too
        except OSError:
            pass  # still open: nothing came within the timeout
    for i, s in enumerate(idle):
        if base + i not in closed:  # a reset leaves no TIME-WAIT here that a later run could trip over
            s.setsockopt(socket.SOL_SOCKET, socket.SO_LINGER, struct.pack("ii", 1, 0))
        s.close()
    assert closed, "the bridge closed none of them"
    time.sleep(0.3)  # our side of the closed ones is gone, the bridge's TIME-WAIT is not
    for port in closed:
        connect(port).close()
