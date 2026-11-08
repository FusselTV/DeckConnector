"""Improv Wi-Fi Serial client for the bridge's console (IMPROV.md in the site notes).

Read-only use on the bench serial: `uv run python -m dbtools.improv state|info|network|name`.
"""

import argparse
import time

STATE, ERROR, RPC, RESULT = 1, 2, 3, 4
WIFI, CURRENT_STATE, INFO, SCAN, HOSTNAME, NAME, NETWORK, SETUP = 1, 2, 3, 4, 5, 6, 7, 0x80
HEADER = b"IMPROV"


def frame(kind: int, data: bytes) -> bytes:
    body = HEADER + bytes([1, kind, len(data)]) + data
    return body + bytes([sum(body) & 0xFF]) + b"\n"


def rpc(cmd: int, *strings: str) -> bytes:
    payload = b"".join(bytes([len(s.encode())]) + s.encode() for s in strings)
    return frame(RPC, bytes([cmd, len(payload)]) + payload)


def strings(data: bytes) -> list[str]:
    """The strings of an RPC result's data (after command and length)."""
    out, pos = [], 0
    while pos < len(data):
        n = data[pos]
        out.append(data[pos + 1 : pos + 1 + n].decode())
        pos += 1 + n
    return out


class Reader:
    """Packets out of a byte stream that also carries log lines."""

    def __init__(self) -> None:
        self.buf = b""
        self.log = b""

    def feed(self, chunk: bytes) -> list[tuple[int, bytes]]:
        self.buf += chunk
        packets = []
        while True:
            at = self.buf.find(HEADER)
            if at < 0:
                keep = max(0, len(self.buf) - len(HEADER) + 1)
                self.log += self.buf[:keep]
                self.buf = self.buf[keep:]
                return packets
            self.log += self.buf[:at]
            self.buf = self.buf[at:]
            if len(self.buf) < 9:
                return packets
            end = 9 + self.buf[8] + 1
            if len(self.buf) < end:
                return packets
            pk, self.buf = self.buf[:end], self.buf[end:]
            if pk[6] == 1 and sum(pk[:-1]) & 0xFF == pk[-1]:
                packets.append((pk[7], pk[9:-1]))


def ask(port, reader: Reader, packet: bytes, until, timeout: float = 5.0) -> list[tuple[int, bytes]]:
    """Send `packet` and collect packets until `until(packets)` is true. `port` has write() and read(n)."""
    port.write(packet)
    got: list[tuple[int, bytes]] = []
    end = time.time() + timeout
    while time.time() < end:
        got += reader.feed(port.read(4096) or b"")
        if until(got):
            return got
    raise TimeoutError(f"no answer to {packet!r}, got {got}")


def result_for(cmd: int):
    """until(): an RPC result for `cmd` or an error other than 0 arrived."""
    return lambda got: any((k == RESULT and d[0] == cmd) or (k == ERROR and d[0]) for k, d in got)


def state_done(got: list[tuple[int, bytes]]) -> bool:
    """until() for CURRENT_STATE: the state, and the URL result when it says provisioned."""
    states = [d[0] for k, d in got if k == STATE]
    return bool(states) and (states[-1] != 4 or any(k == RESULT for k, _ in got))


def main() -> None:
    import serial

    from dbtools.site import SERIAL, need

    cmds = {"state": CURRENT_STATE, "info": INFO, "network": NETWORK, "name": NAME}
    ap = argparse.ArgumentParser()
    ap.add_argument("cmd", choices=cmds)
    a = ap.parse_args()
    s = serial.serial_for_url(need(SERIAL, "serial"), 115200, timeout=0.2, do_not_open=True)
    s.dtr = s.rts = False  # high on open holds the board in reset
    s.open()
    cmd = cmds[a.cmd]
    until = state_done if cmd == CURRENT_STATE else result_for(cmd)
    for kind, data in ask(s, Reader(), rpc(cmd), until):
        print(
            {STATE: "state", ERROR: "error", RESULT: "result"}.get(kind, kind),
            data[0] if kind != RESULT else strings(data[2:]),
        )


if __name__ == "__main__":
    main()
