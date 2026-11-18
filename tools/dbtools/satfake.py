"""Companion's Satellite API on this machine, enough for the bridge to connect, take pictures and enter a PIN."""

import socket
import socketserver
import threading


class Handler(socketserver.StreamRequestHandler):
    server: "FakeSatellite"

    def handle(self) -> None:
        srv = self.server
        srv.conns += 1
        srv.conn = self.connection
        self.wfile.write(b"BEGIN CompanionVersion=fake ApiVersion=1.16.0\nCAPS BITMAP_FORMATS=rgb ROTARY_AMOUNT=1\n")
        for raw in self.rfile:
            line = raw.decode().strip()
            srv.lines.append(line)
            cmd = line.split(" ", 1)[0]
            if cmd == "ADD-DEVICE":
                srv.device = line.split('DEVICEID="', 1)[1].split('"', 1)[0]
                self.wfile.write(f'ADD-DEVICE OK DEVICEID="{srv.device}"\n'.encode())
            elif cmd == "PING":
                self.wfile.write(b"PONG\n")
            elif cmd == "PINCODE-KEY":
                self.wfile.write(f'PINCODE-KEY OK DEVICEID="{srv.device}"\n'.encode())

    def finish(self) -> None:
        self.server.conn = None


class FakeSatellite(socketserver.ThreadingTCPServer):
    allow_reuse_address = True  # read while binding: set on the instance it comes too late
    daemon_threads = True

    def __init__(self, port: int):
        super().__init__(("0.0.0.0", port), Handler)
        self.conns = 0
        self.lines: list[str] = []
        self.device: str | None = None
        self.conn: socket.socket | None = None
        threading.Thread(target=self.serve_forever, daemon=True).start()

    def send(self, line: str) -> None:
        self.sendall(line.encode() + b"\n")

    def sendall(self, data: bytes) -> None:
        if self.conn is None:
            raise RuntimeError("no bridge connected to the Satellite fake")
        self.conn.sendall(data)
