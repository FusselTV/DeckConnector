"""A small deckbridge server: accepts bridges, sends images and collects input."""

import asyncio
import contextlib
import json
import socket
import time

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
        self.role_changed = asyncio.Event()
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
            fut = self._waiting.pop(msg.seq, None)
            if fut and not fut.done():
                fut.set_result(msg)
        elif isinstance(msg, p.Input):
            self.inputs.put_nowait(msg)
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
            for fut in self._waiting.values():
                if not fut.done():
                    fut.set_exception(ConnectionError("bridge disconnected"))


def self_signed():
    """An SSL context with a new P-256 certificate, and that certificate's SHA-256 in hex."""
    import datetime
    import ssl
    import tempfile

    from cryptography import x509
    from cryptography.hazmat.primitives import hashes, serialization
    from cryptography.hazmat.primitives.asymmetric import ec
    from cryptography.x509.oid import NameOID

    key = ec.generate_private_key(ec.SECP256R1())
    name = x509.Name([x509.NameAttribute(NameOID.COMMON_NAME, "dbtools")])
    now = datetime.datetime.now(datetime.UTC)
    cert = (
        x509.CertificateBuilder()
        .subject_name(name)
        .issuer_name(name)
        .public_key(key.public_key())
        .serial_number(x509.random_serial_number())
        .not_valid_before(now - datetime.timedelta(days=1))
        .not_valid_after(now + datetime.timedelta(days=30))
        .sign(key, hashes.SHA256())
    )
    ctx = ssl.SSLContext(ssl.PROTOCOL_TLS_SERVER)
    with tempfile.NamedTemporaryFile("wb", suffix=".pem") as f:
        f.write(cert.public_bytes(serialization.Encoding.PEM))
        f.write(
            key.private_bytes(
                serialization.Encoding.PEM, serialization.PrivateFormat.PKCS8, serialization.NoEncryption()
            )
        )
        f.flush()
        ctx.load_cert_chain(f.name)
    return ctx, cert.fingerprint(hashes.SHA256()).hex()


class Server:
    def __init__(
        self,
        port: int = 8765,
        name: str = "dbtools",
        announce: bool = False,
        token: str = "",
        ping: bool = True,
        tls: bool = False,
    ):
        """`tls`: wss:// with a certificate made for this run; `fingerprint` is its SHA-256 for the bridge."""
        self.port = port
        self.name = name
        self.announce = announce
        self.token = token
        self.ping = ping
        self.bridges: dict[str, Bridge] = {}
        self.connected: asyncio.Queue[Bridge] = asyncio.Queue()
        self._server = None
        self._zc: AsyncZeroconf | None = None
        self.ssl, self.fingerprint = self_signed() if tls else (None, "")

    @property
    def url(self) -> str:
        return f"{'wss' if self.ssl else 'ws'}://{local_ip()}:{self.port}/deckbridge"

    async def _handler(self, ws: ServerConnection) -> None:
        if self.token and ws.request.headers.get("Authorization") != f"Bearer {self.token}":
            await ws.close(4001, "bad token")
            return
        bridge = Bridge(ws)
        task = asyncio.create_task(bridge.run())
        # hello always comes first; a bridge is filed under its id, so one without a hello is not kept
        for _ in range(1000):
            if bridge.hello or bridge.closed.is_set():
                break
            await asyncio.sleep(0.01)
        if not bridge.hello:
            await ws.close(4002, "no hello")
            await task
            return
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
            subprotocols=["deckconnector.v1", "deckbridge.v1"],
            max_size=2**22,
            ping_interval=5 if self.ping else None,
            ping_timeout=15,
            ssl=self.ssl,
        )
        if self.announce:
            self._zc = AsyncZeroconf(ip_version=IPVersion.V4Only)
            info = AsyncServiceInfo(
                "_deckconnector._tcp.local.",
                f"{self.name}._deckconnector._tcp.local.",
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
