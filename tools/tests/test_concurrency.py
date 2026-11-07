"""Requests on port 80 and 443 at the same time: two server tasks share the handlers."""

import asyncio
import http.client
import json
import threading

from conftest import HOST, TLS, api, put_config
from test_https import AUTH
from websockets.asyncio.client import connect


def put_both(rounds: int):
    """Yield after each round in which port 80 set the brightness and port 443 the satellite pin at once."""
    plain, tls = http.client.HTTPConnection(HOST, timeout=10), http.client.HTTPSConnection(HOST, context=TLS)
    start = threading.Barrier(2)

    def put(conn, body):
        start.wait()
        conn.request("PUT", "/api/config", json.dumps(body), AUTH)
        conn.getresponse().read()

    for i in range(rounds):
        bright, pin = 20 + i % 60, f"{i:064x}"
        other = threading.Thread(target=put, args=(tls, {"satellite_pin": pin}))
        other.start()
        put(plain, {"brightness": bright})
        other.join()
        yield bright, pin


def test_config_changes_on_both_ports_are_never_lost(servers):  # a test pin would drop the owner's satellite link
    before = json.loads(api("GET", "/api/config")[1])
    lost = []
    try:
        for i, wanted in enumerate(put_both(60)):
            cfg = json.loads(api("GET", "/api/config")[1])
            if (cfg["brightness"], cfg["satellite_pin"]) != wanted:
                lost.append(i)
    finally:
        put_config({"brightness": before["brightness"], "satellite_pin": before["satellite_pin"]})
    assert not lost, f"a change was lost in rounds {lost}"


async def test_live_sockets_on_both_ports_while_presses_go_out(bridges):
    """A wss listener sends presses: its replies come from the server task, the press events from another task,
    both on one TLS session. Meanwhile listeners on port 80 come and go, and each must start with its own state."""
    # the presses must reach the test server: while the bridge draws they would walk its own menu
    for _ in range(30):
        writer = json.loads((await asyncio.to_thread(api, "GET", "/api/status"))[1])["writer"]
        if writer.startswith("server"):
            break
        await asyncio.sleep(0.5)
    assert writer.startswith("server"), f"{writer} draws"
    login = {"Authorization": AUTH["Authorization"]}

    async def presses():
        async with connect(f"wss://{HOST}/api/ws", ssl=TLS, additional_headers=login) as ws:
            results = 0
            for i in range(150):
                await ws.send(json.dumps({"t": "input", "id": i, "key": i % 8}))
            while results < 150:
                msg = json.loads(await asyncio.wait_for(ws.recv(), 10))
                results += msg["t"] == "result"

    async def listeners():
        for _ in range(60):
            async with connect(f"ws://{HOST}/api/ws", additional_headers=login) as ws:
                assert json.loads(await asyncio.wait_for(ws.recv(), 5))["t"] == "state"

    await asyncio.gather(presses(), listeners())
