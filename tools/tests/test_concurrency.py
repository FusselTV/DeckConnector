"""Requests on port 80 and 443 at the same time: two server tasks share the handlers."""

import http.client
import json
import threading

from conftest import HOST, api
from test_https import AUTH, TLS


def put_both(rounds: int):
    """Yield after each round in which port 80 set the brightness and port 443 the satellite pin at once."""
    plain, tls = http.client.HTTPConnection(HOST, timeout=10), http.client.HTTPSConnection(HOST, context=TLS)
    start = threading.Barrier(2)

    def put(conn, body):
        start.wait()
        conn.request("PUT", "/api/config", json.dumps(body), AUTH)
        conn.getresponse().read()

    for i in range(rounds):
        bright, pin = 20 + i % 60, f"{i:064x}"
        other = threading.Thread(target=put, args=(tls, {"satellite_pin": pin}))
        other.start()
        put(plain, {"brightness": bright})
        other.join()
        yield bright, pin


def test_config_changes_on_both_ports_are_never_lost():
    before = json.loads(api("GET", "/api/config")[1])
    lost = []
    try:
        for i, wanted in enumerate(put_both(150)):
            cfg = json.loads(api("GET", "/api/config")[1])
            if (cfg["brightness"], cfg["satellite_pin"]) != wanted:
                lost.append(i)
    finally:
        body = {"brightness": before["brightness"], "satellite_pin": before["satellite_pin"]}
        api("PUT", "/api/config", json.dumps(body).encode())
    assert not lost, f"a change was lost in rounds {lost}"
