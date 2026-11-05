"""A bad network between the bridge and a server: delays, a pause, a connection that dies without closing."""

import asyncio
import json
import random
import time

from conftest import api, jpeg

from dbtools.protocol import AckResult

KEY0 = (13, 12, 120, 120)
PROXY_PORT = 8775


class Proxy:
    """Forwards to the test server. Every chunk waits up to `jitter` s; `paused` holds all traffic; `cut`
    stops forwarding for good without closing, like a dead switch port."""

    def __init__(self, target_port: int):
        self.target_port, self.jitter, self.paused, self.cut = target_port, 0.0, False, False

    async def pipe(self, reader, writer):
        try:
            while data := await reader.read(65536):
                while self.paused and not self.cut:
                    await asyncio.sleep(0.05)
                if self.cut:
                    await asyncio.sleep(3600)
                await asyncio.sleep(random.uniform(0, self.jitter))
                writer.write(data)
                await writer.drain()
        except (ConnectionError, asyncio.CancelledError):
            pass
        writer.close()

    async def handle(self, reader, writer):
        up_reader, up_writer = await asyncio.open_connection("127.0.0.1", self.target_port)
        await asyncio.gather(self.pipe(reader, up_writer), self.pipe(up_reader, writer))

    async def start(self):
        self.server = await asyncio.start_server(self.handle, "0.0.0.0", PROXY_PORT)


async def put_servers(urls):
    code, _ = await asyncio.to_thread(api, "PUT", "/api/config", json.dumps({"servers": urls}).encode())
    assert code == 200


async def test_a_bad_network_to_a_server(servers, bridges):
    first, second = servers
    proxy = Proxy(first.port)
    await proxy.start()
    via_proxy = first.url.replace(f":{first.port}/", f":{PROXY_PORT}/")
    while not first.connected.empty():
        first.connected.get_nowait()
    await put_servers([via_proxy, second.url])
    try:
        bridge = await first.wait_bridge(20)

        # jitter of up to 150 ms on every chunk: every image still arrives, the link stays
        proxy.jitter = 0.15
        for i in range(20):
            ack = await bridge.send_image(KEY0, jpeg((i * 12, 80, 200 - i * 9), (120, 120)), timeout=10)
            assert ack.result in (AckResult.SHOWN, AckResult.SUPERSEDED)
        proxy.jitter = 0.0

        # a pause of 3 s, shorter than the bridge's silence limit: the link survives it
        proxy.paused = True
        await asyncio.sleep(3)
        proxy.paused = False
        assert (await bridge.send_image(KEY0, jpeg((0, 200, 0), (120, 120)))).result == AckResult.SHOWN
        assert first.connected.empty(), "no new connection after a short pause"

        # the path dies without a close: the bridge notices by itself and connects again
        t0 = time.monotonic()
        proxy.cut = True
        await asyncio.sleep(1)
        proxy.cut = False
        back = await first.wait_bridge(30)
        took = time.monotonic() - t0
        print(f"connected again {took:.1f} s after the path died")
        assert back is not bridge and took < 15
        assert (await back.send_image(KEY0, jpeg((200, 0, 0), (120, 120)))).result == AckResult.SHOWN
    finally:
        await put_servers([first.url, second.url])
        proxy.server.close()
        await first.wait_bridge(30)
