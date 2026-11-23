"""wss:// links: pairing pins the server's certificate, and the bridge talks TLS only to a server with that one."""

import asyncio
import json

from conftest import api, status

from dbtools.server import Server


async def put(body: dict) -> None:
    code, reply = await asyncio.to_thread(api, "PUT", "/api/config", json.dumps(body).encode())
    assert code == 200, reply


async def connected(url: str) -> bool:
    return any(x["host"] == url and x["connected"] for x in (await asyncio.to_thread(status))["sources"])


async def stays_away(server: Server, url: str, secs: float = 8) -> bool:
    """Several tries: the bridge retries a failed link every 2 s, and a TLS handshake adds to that."""
    for _ in range(int(secs * 2)):
        if server.bridges or await connected(url):
            return False
        await asyncio.sleep(0.5)
    return True


async def test_wss_pairs_and_then_needs_the_pinned_certificate():
    server = await Server(8771, "wss", ping=False, tls=True).start()
    before = json.loads((await asyncio.to_thread(api, "GET", "/api/config"))[1])
    servers, pins = before["servers"], before["server_pins"]
    slot = servers.index("") if "" in servers[:2] else 1  # the bridge links to two servers at most

    def at_slot(values: list, value: str) -> list:
        return [*values[:slot], value, *values[slot + 1 :]]

    try:
        await put({"servers": at_slot(servers, server.url), "server_pins": at_slot(pins, "")})
        await server.wait_bridge(20)  # the code is confirmed at once, the bridge comes back with its token
        cfg = json.loads((await asyncio.to_thread(api, "GET", "/api/config"))[1])
        assert cfg["server_pins"][slot] == server.fingerprint

        await put({"server_pins": at_slot(pins, "00" * 32)})  # another certificate: the bridge goes and stays away
        for _ in range(20):
            if not server.bridges:
                break
            await asyncio.sleep(0.5)
        assert await stays_away(server, server.url)

        await put({"server_pins": at_slot(pins, "")})  # paired without a pin: no public CA signed it, so no TLS
        assert await stays_away(server, server.url)
    finally:
        await put({"servers": servers, "server_pins": pins})
        await server.stop()
