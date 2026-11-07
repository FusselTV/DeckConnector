"""The wizard tests a Companion address before it saves it: a fake Satellite API on this machine answers."""

import json
import socket
import threading

import pytest
from conftest import TARGET, api

from dbtools.server import local_ip


def probe(addr: str) -> dict:
    code, body = api("POST", "/api/sources/probe", json.dumps({"addr": addr}).encode(), timeout=15)
    assert code == 200, body
    return json.loads(body)


@pytest.mark.sim
def test_a_bad_probe_request_is_refused():
    assert api("POST", "/api/sources/probe", b'{"addr": ""}')[0] == 400


@pytest.mark.skipif(TARGET == "sim", reason="the simulator reaches no Companion")
def test_the_probe_reads_the_companion_version():
    srv = socket.create_server(("0.0.0.0", 0))
    port = srv.getsockname()[1]

    def greet():
        conn, _ = srv.accept()
        conn.sendall(b"BEGIN CompanionVersion=4.1.0 ApiVersion=1.8.0\n")
        conn.close()

    threading.Thread(target=greet, daemon=True).start()
    try:
        assert probe(f"{local_ip()}:{port}") == {"ok": True, "error": "", "version": "4.1.0"}
    finally:
        srv.close()
    closed = probe(f"{local_ip()}:{port}")
    assert not closed["ok"] and closed["error"], closed
