"""A small surface host (protocol v2): pairs bridges, fills their cache, draws and collects input."""

import asyncio
import contextlib
import itertools
import secrets
import socket
import ssl
import time

import websockets
from websockets.asyncio.server import Server as WsServer
from websockets.asyncio.server import ServerConnection, serve
from websockets.typing import Subprotocol

from . import protocol as p


def now_us() -> int:
    return time.monotonic_ns() // 1000


def local_ip(towards: str = "192.0.2.1") -> str:
    with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as s:
        s.connect((towards, 9))
        return s.getsockname()[0]


class Bridge:
    """One connected bridge."""

    def __init__(self, ws: ServerConnection):
        self.ws = ws
        self.hello: dict = {}
        self.decks: dict[int, dict] = {}
        self.credits: dict[tuple[int, int], p.Credit] = {}
        self.states: dict[int, dict] = {}
        self.errors: list[dict] = []
        self.inputs: asyncio.Queue[p.Input] = asyncio.Queue()
        self.removed: asyncio.Queue[int] = asyncio.Queue()
        self.closed = asyncio.Event()
        self._ids = itertools.count(1)
        self._waiting: dict[tuple[bool, int], asyncio.Future[p.Shown]] = {}

    @property
    def id(self) -> str:
        return self.hello.get("board", {}).get("id", "?")

    @property
    def pair_code(self) -> str | None:
        return self.hello.get("pair_code")

    def target(self, deck: int, kind: str = "key", n: int = 0) -> dict:
        """The n-th target of that kind on the deck."""
        return [t for t in self.decks[deck]["targets"] if t["kind"] == kind][n]

    async def send(self, data: bytes) -> None:
        await self.ws.send(data)

    async def role(self, active: bool = True) -> None:
        await self.send(p.role(active))

    async def put(self, slot: int, image: bytes, w: int, h: int, fmt: int = p.JPEG) -> None:
        await self.send(p.put(slot, image, w, h, fmt))

    def _expect(self, stream: bool, ident: int) -> asyncio.Future[p.Shown]:
        fut = asyncio.get_running_loop().create_future()
        self._waiting[stream, ident] = fut
        return fut

    async def frame(self, deck: int, shots: list[tuple[int, int]]) -> asyncio.Future[p.Shown]:
        """Send without waiting; the future resolves with its SHOWN."""
        ident = next(self._ids)
        fut = self._expect(False, ident)
        await self.send(p.frame(deck, ident, shots))
        return fut

    async def show(self, deck: int, shots: list[tuple[int, int]], timeout: float = 5) -> p.Shown:
        return await asyncio.wait_for(await self.frame(deck, shots), timeout)

    async def stream(self, deck: int, target: int, image: bytes, fmt: int = p.JPEG) -> asyncio.Future[p.Shown]:
        seq = next(self._ids)
        fut = self._expect(True, seq)
        await self.send(p.stream(deck, target, seq, image, fmt))
        return fut

    async def draw(self, deck: int, target: dict, image: bytes, slot: int, fmt: int = p.JPEG) -> p.Shown:
        """One picture on one target: PUT, then FRAME."""
        await self.put(slot, image, target["w"], target["h"], fmt)
        return await self.show(deck, [(target["id"], slot)])

    def _handle(self, msg: p.Input | p.Shown | p.Credit | p.Message) -> None:
        if isinstance(msg, p.Input):
            self.inputs.put_nowait(msg)
        elif isinstance(msg, p.Shown):
            fut = self._waiting.pop((msg.stream, msg.id), None)
            if fut and not fut.done():
                fut.set_result(msg)
        elif isinstance(msg, p.Credit):
            self.credits[msg.deck, msg.display] = msg
        elif msg.type == p.HELLO:
            self.hello = msg.body
            self.decks = {d["deck"]: d for d in msg.body["decks"]}
        elif msg.type == p.DECK_ADDED:
            self.decks[msg.deck] = msg.body
        elif msg.type == p.DECK_REMOVED:
            self.decks.pop(msg.deck, None)
            self.removed.put_nowait(msg.deck)
        elif msg.type == p.STATE:
            self.states[msg.deck] = msg.body
        elif msg.type == p.ERROR:
            self.errors.append(msg.body | {"deck": msg.deck})

    async def run(self) -> None:
        try:
            async for data in self.ws:
                if isinstance(data, bytes):
                    self._handle(p.parse(data))
        except websockets.ConnectionClosed:
            pass
        finally:
            self.closed.set()
            for fut in self._waiting.values():
                if not fut.done():
                    fut.set_exception(ConnectionError("bridge disconnected"))


def self_signed() -> tuple[ssl.SSLContext, str]:
    """An SSL context with a new P-256 certificate, and that certificate's SHA-256 in hex."""
    import datetime
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
        ping: bool = True,
        tls: bool = False,
        auto_pair: bool = True,
        tokens: set[str] | None = None,
    ):
        """`tls`: wss:// with a certificate made for this run, its SHA-256 in `fingerprint`. `auto_pair` confirms
        every pairing code at once, as an admin would. `tokens`: the ones it handed out, to share with another
        run on the same port."""
        self.port = port
        self.name = name
        self.ping = ping
        self.auto_pair = auto_pair
        self.tokens: set[str] = set() if tokens is None else tokens
        self.bridges: dict[str, Bridge] = {}
        self.connected: asyncio.Queue[Bridge] = asyncio.Queue()
        self.refused = 0
        self._server: WsServer | None = None
        self.ssl: ssl.SSLContext | None = None
        self.fingerprint = ""
        if tls:
            self.ssl, self.fingerprint = self_signed()

    @property
    def url(self) -> str:
        return f"{'wss' if self.ssl else 'ws'}://{local_ip()}:{self.port}/surface"

    async def pair(self, bridge: Bridge) -> str:
        """Confirm the bridge's code: it gets a token, keeps it and connects again with it."""
        token = secrets.token_urlsafe(24)
        self.tokens.add(token)
        await bridge.send(p.paired(token))
        return token

    async def _handler(self, ws: ServerConnection) -> None:
        auth = ws.request.headers.get("Authorization", "") if ws.request else ""
        if auth and auth.removeprefix("Bearer ") not in self.tokens:
            self.refused += 1
            await ws.close(4001, "unknown token")
            return
        bridge = Bridge(ws)
        task = asyncio.create_task(bridge.run())
        for _ in range(500):  # HELLO comes first; a bridge is filed under its board id
            if bridge.hello or bridge.closed.is_set():
                break
            await asyncio.sleep(0.01)
        if not bridge.hello:
            await ws.close(4002, "no hello")
            await task
            return
        if bridge.pair_code and self.auto_pair:
            await self.pair(bridge)
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
            subprotocols=[Subprotocol("deckassistant.v2")],
            max_size=2**22,
            ping_interval=2 if self.ping else None,
            ping_timeout=6,
            ssl=self.ssl,
        )
        return self

    async def stop(self) -> None:
        if self._server:
            self._server.close()
            with contextlib.suppress(Exception):
                await asyncio.wait_for(self._server.wait_closed(), 3)

    async def wait_bridge(self, timeout: float = 30, paired: bool = True) -> Bridge:
        """The next bridge to connect; with `paired`, the next one that came with a token."""
        end = asyncio.get_running_loop().time() + timeout
        while True:
            bridge = await asyncio.wait_for(self.connected.get(), end - asyncio.get_running_loop().time())
            if not paired or not bridge.pair_code:
                return bridge
