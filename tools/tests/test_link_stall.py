"""A host that goes quiet for a while keeps its link, and the next connect resumes the TLS session."""

import asyncio
import json

from conftest import api

from dbtools.server import Server


async def put(body: dict) -> None:
    code, reply = await asyncio.to_thread(api, "PUT", "/api/config", json.dumps(body).encode())
    assert code == 200, reply


async def test_a_quiet_host_keeps_its_link_and_a_new_connect_resumes_tls():
    server = await Server(8776, "stall", ping=False, tls=True, auto_pair=False).start()
    servers = json.loads((await asyncio.to_thread(api, "GET", "/api/config"))[1])["servers"]
    slot = servers.index("") if "" in servers[:2] else 1
    try:
        await put({"servers": [*servers[:slot], server.url, *servers[slot + 1 :]]})
        bridge = await server.wait_bridge(20, paired=False)

        # 9 s without a byte from the host, its pongs included: longer than the 6 s the bridge once allowed
        bridge.ws.transport.pause_reading()
        await asyncio.sleep(9)
        bridge.ws.transport.resume_reading()
        await asyncio.sleep(2)
        assert not bridge.closed.is_set(), "the link lived through the quiet host"
        assert server.connected.empty(), "no new connection"

        await bridge.ws.close()
        again = await server.wait_bridge(20, paired=False)
        assert again.ws.transport.get_extra_info("ssl_object").session_reused, "the handshake resumed the session"
    finally:
        await put({"servers": servers})
        await server.stop()
