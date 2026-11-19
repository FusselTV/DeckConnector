"""The simulators' mDNS, on a network of this test run alone: a user and network namespace with a veth pair, dca
(10.77.0.1) and dcb (10.77.0.2), that never reaches the LAN or the board. `uv run pytest simnet`, without -n."""

import atexit
import itertools
import os
import subprocess
import time
import urllib.request
from pathlib import Path

import pytest
from zeroconf import ServiceBrowser, ServiceInfo, ServiceListener, Zeroconf

from dbtools.simdeck import ELF as SIMDECK
from dbtools.simdeck import SimDeck

SIM = Path(__file__).resolve().parents[2] / "firmware/sim/build/deckbridge_sim.elf"
WORK = Path.home() / ".cache/simnet"
_ports = itertools.count(18750)


def _private_network() -> None:
    uid, gid = os.getuid(), os.getgid()
    os.unshare(os.CLONE_NEWUSER | os.CLONE_NEWNET)  # before pytest starts a thread: unshare refuses then
    Path("/proc/self/setgroups").write_text("deny")
    Path("/proc/self/uid_map").write_text(f"0 {uid} 1")
    Path("/proc/self/gid_map").write_text(f"0 {gid} 1")
    for cmd in (
        "ip link set lo up",
        "ip link add dca type veth peer name dcb",
        "ip addr add 10.77.0.1/24 dev dca",
        "ip addr add 10.77.0.2/24 dev dcb",
        "ip link set dca up",
        "ip link set dcb up",
    ):
        subprocess.run(cmd.split(), check=True)
    # both ends are on this host: without it the kernel drops what one sends to the other as a martian
    for i in ("all", "dca", "dcb"):
        Path(f"/proc/sys/net/ipv4/conf/{i}/accept_local").write_text("1")


_private_network()


@pytest.fixture
def sim():
    """`sim(ifname)`: firmware/sim answering mDNS on that interface; its HTTP port comes back."""
    if not SIM.exists():
        pytest.skip(f"build {SIM.parent.parent} first")
    procs = []

    def start(ifname: str, **env: str) -> int:
        port = next(_ports)
        WORK.mkdir(parents=True, exist_ok=True)
        log = (WORK / f"sim-{port}.log").open("wb")
        env = {**os.environ, "DB_SIM_PORT": str(port), "DB_SIM_PASSWORD": "simnet-pass", "DB_SIM_MDNS_IF": ifname, **env}
        proc = subprocess.Popen([SIM], env=env, stdout=log, stderr=subprocess.STDOUT)
        atexit.register(proc.kill)
        procs.append(proc)
        for _ in range(100):
            try:
                urllib.request.urlopen(f"http://127.0.0.1:{port}/", timeout=1)
                return port
            except OSError:
                assert proc.poll() is None, (WORK / f"sim-{port}.log").read_text(errors="replace")
                time.sleep(0.05)
        raise RuntimeError("the simulator did not start")

    yield start
    for proc in procs:
        proc.kill()
        proc.wait()


@pytest.fixture
def simdeck(request):
    """`simdeck(*models)`: firmware/simdeck on those decks, answering mDNS on dca."""
    if not SIMDECK.exists():
        pytest.skip(f"build {SIMDECK.parent.parent} first")
    sims = []

    def start(*models: str, **env: str) -> SimDeck:
        s = SimDeck(list(models), next(_ports), WORK / request.node.name, DB_SIM_MDNS_IF="dca", **env).start()
        sims.append(s)
        return s

    yield start
    for s in sims:
        s.stop()


@pytest.fixture
def zc():
    """A browser on dcb: it hears dca over the veth and dcb by multicast loopback."""
    z = Zeroconf(interfaces=["10.77.0.2"])
    yield z
    z.close()


class Seen(ServiceListener):
    def __init__(self):
        self.names: set[str] = set()

    def add_service(self, zc, type_, name):
        self.names.add(name)

    def remove_service(self, zc, type_, name):
        self.names.discard(name)

    def update_service(self, zc, type_, name):
        pass


def browse(zc: Zeroconf, kind: str, until, secs: float = 10) -> dict[str, ServiceInfo]:
    """Instances of `kind` once `until(names)` holds, with their records."""
    seen = Seen()
    browser = ServiceBrowser(zc, kind, seen)
    try:
        end = time.monotonic() + secs
        while not until(seen.names) and time.monotonic() < end:
            time.sleep(0.1)
        assert until(seen.names), f"{kind}: {sorted(seen.names)}"
        infos = {n: zc.get_service_info(kind, n, 3000) for n in seen.names}
    finally:
        browser.cancel()
    assert all(infos.values()), infos
    return infos
