"""A small deckbridge server: accepts bridges, sends images and collects input."""

import asyncio
import contextlib
import json
import socket
import time
from collections.abc import Callable

import websockets
from websockets.asyncio.server import ServerConnection, serve
from zeroconf import IPVersion
from zeroconf.asyncio import AsyncServiceInfo, AsyncZeroconf

from . import protocol as p


def now_us() -> int:
    return time.monotonic_ns() // 1000


def local_ip(towards: str = "192.168.1.1") -> str:
    with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as s:
        s.connect((towards, 9))
        return s.getsockname()[0]


class Bridge:
    """One connected bridge."""

    def __init__(self, ws: ServerConnection):
        self.ws = ws
        self.hello: dict = {}
        self.device: dict = {}
        self.status: dict = {}
        self.ota: list[dict] = []
        self.active = False
        self.inputs: asyncio.Queue[p.Input] = asyncio.Queue()
        self.acks: list[p.Ack] = []
        self.role_changed = asyncio.Event()
        self.device_changed = asyncio.Event()
        self.on_input: Callable[[p.Input], None] | None = None
        self._seq = 0
        self._waiting: dict[int, asyncio.Future] = {}
        self._time: asyncio.Future | None = None
        self.closed = asyncio.Event()

    @property
    def id(self) -> str:
        return self.hello.get("id", "?")

    def next_seq(self) -> int:
        self._seq = (self._seq + 1) & 0xFFFFFFFF
        return self._seq

    async def post_image(self, rect: tuple, jpeg: bytes, display: int = 0) -> asyncio.Future:
        """Send without waiting. The future resolves with the Ack."""
        seq = self.next_seq()
        fut = asyncio.get_running_loop().create_future()
        self._waiting[seq] = fut
        await self.ws.send(p.image(display, *rect, seq, jpeg))
        return fut

    async def send_image(self, rect, jpeg: bytes, display: int = 0, timeout: float = 5) -> p.Ack:
        fut = await self.post_image(rect, jpeg, display)
        return await asyncio.wait_for(fut, timeout)

    async def clock_offset(self, rounds: int = 20) -> int:
        """Bridge clock minus server clock in microseconds, from the round trip with the smallest delay."""
        best = None
        for _ in range(rounds):
            self._time = asyncio.get_running_loop().create_future()
            sent = now_us()
            await self.ws.send(p.time_request(sent))
            _, bridge = await asyncio.wait_for(self._time, 2)
            back = now_us()
            if best is None or back - sent < best[0]:
                best = (back - sent, bridge - (sent + back) // 2)
            await asyncio.sleep(0.005)
        return best[1]

    async def send_json(self, msg: dict) -> None:
        await self.ws.send(json.dumps(msg))

    def _handle_text(self, msg: dict) -> None:
        kind = msg.get("t")
        if kind == "hello":
            self.hello = msg
            self.active = msg.get("active", False)
        elif kind == "device":
            self.device = msg
            self.device_changed.set()
        elif kind == "role":
            self.active = msg["active"]
            self.role_changed.set()
        elif kind == "status":
            self.status = msg
        elif kind == "ota":
            self.ota.append(msg)

    def _handle_binary(self, data: bytes) -> None:
        msg = p.parse(data)
        if isinstance(msg, p.Ack):
            self.acks.append(msg)
            fut = self._waiting.pop(msg.seq, None)
            if fut and not fut.done():
                fut.set_result(msg)
        elif isinstance(msg, p.Input):
            self.inputs.put_nowait(msg)
            if self.on_input:
                self.on_input(msg)
        elif self._time and not self._time.done():
            self._time.set_result(msg)

    async def run(self) -> None:
        try:
            async for data in self.ws:
                if isinstance(data, str):
                    self._handle_text(json.loads(data))
                else:
                    self._handle_binary(data)
        except websockets.ConnectionClosed:
            pass
        finally:
            self.closed.set()


class Server:
    def __init__(self, port: int = 8765, name: str = "dbtools", announce: bool = False, token: str = ""):
        self.port = port
        self.name = name
        self.announce = announce
        self.token = token
        self.bridges: dict[str, Bridge] = {}
        self.connected: asyncio.Queue[Bridge] = asyncio.Queue()
        self._server = None
        self._zc: AsyncZeroconf | None = None

    @property
    def url(self) -> str:
        return f"ws://{local_ip()}:{self.port}/deckbridge"

    async def _handler(self, ws: ServerConnection) -> None:
        if self.token and ws.request.headers.get("Authorization") != f"Bearer {self.token}":
            await ws.close(4001, "bad token")
            return
        bridge = Bridge(ws)
        task = asyncio.create_task(bridge.run())
        # hello always comes first
        for _ in range(200):
            if bridge.hello or bridge.closed.is_set():
                break
            await asyncio.sleep(0.01)
        self.bridges[bridge.id] = bridge
        await self.connected.put(bridge)
        await task
        if self.bridges.get(bridge.id) is bridge:
            del self.bridges[bridge.id]

    async def start(self) -> "Server":
        self._server = await serve(
            self._handler,
            "0.0.0.0",
            self.port,
            subprotocols=["deckbridge.v1"],
            max_size=2**22,
            ping_interval=5,
            ping_timeout=15,
        )
        if self.announce:
            self._zc = AsyncZeroconf(ip_version=IPVersion.V4Only)
            info = AsyncServiceInfo(
                "_deckbridge._tcp.local.",
                f"{self.name}._deckbridge._tcp.local.",
                addresses=[socket.inet_aton(local_ip())],
                port=self.port,
                properties={"path": "/deckbridge"},
            )
            await self._zc.async_register_service(info)
        return self

    async def stop(self) -> None:
        if self._zc:
            await self._zc.async_unregister_all_services()
            await self._zc.async_close()
        if self._server:
            self._server.close()
            with contextlib.suppress(Exception):
                await asyncio.wait_for(self._server.wait_closed(), 3)

    async def wait_bridge(self, timeout: float = 30) -> Bridge:
        return await asyncio.wait_for(self.connected.get(), timeout)
