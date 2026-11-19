"""Behind a trusted reverse proxy, X-Forwarded-For names the client for lockouts, challenges and the setup check;
from anyone else the header counts for nothing."""

import json
import time
import urllib.request

import pytest
from conftest import HOST, PASSWORD, TARGET, api, get_config, put_config
from test_passkeys import Authenticator, challenge

pytestmark = [pytest.mark.sim, pytest.mark.skipif(TARGET != "sim", reason="the simulator is its own proxy's peer")]


def login(password: str, xff: str | None = None) -> int:
    headers = {"X-DeckConnector": "1"} | ({"X-Forwarded-For": xff} if xff else {})
    req = urllib.request.Request(
        f"http://{HOST}/api/login", data=json.dumps({"password": password}).encode(), method="POST", headers=headers
    )
    try:
        with urllib.request.urlopen(req, timeout=10) as r:
            return r.status
    except urllib.error.HTTPError as e:
        return e.code


def lock_out(xff: str | None) -> None:
    for _ in range(5):
        assert login("wrong-password", xff) == 403
    assert login("wrong-password", xff) == 429


def unlock(xff: str | None = None) -> None:
    for _ in range(80):
        if login(PASSWORD, xff) == 200:
            return
        time.sleep(1)
    raise AssertionError("still locked")


@pytest.fixture
def proxy(request):
    put_config({"trusted_proxy": request.param})
    yield request.param
    unlock()
    put_config({"trusted_proxy": ""})


def test_trusted_proxy_is_checked_and_live():
    for bad in ["nope", "1.2.3.4:80", "256.1.1.1", "01.2.3.4", "::1"]:
        code, body = api("PUT", "/api/config", json.dumps({"trusted_proxy": bad}).encode())
        assert code == 400 and b"trusted_proxy" in body, bad
    code, body = api("PUT", "/api/config", b'{"trusted_proxy": "192.168.1.15"}')
    assert code == 200 and json.loads(body)["restart"] is False
    assert get_config()["trusted_proxy"] == "192.168.1.15"
    put_config({"trusted_proxy": ""})


@pytest.mark.parametrize("proxy", ["127.0.0.1"], indirect=True)
def test_each_client_behind_the_proxy_has_its_own_lockout(proxy):
    lock_out("10.0.0.1")
    assert login(PASSWORD, "10.0.0.2") == 200
    assert login(PASSWORD, "6.6.6.6, 10.0.0.1") == 429  # the last entry is the one the proxy added
    unlock("10.0.0.1")


@pytest.mark.parametrize("proxy", ["", "127.0.0.2"], indirect=True)
def test_a_header_from_anyone_else_counts_for_nothing(proxy):
    lock_out("10.0.0.1")
    assert login(PASSWORD, "10.0.0.2") == 429


@pytest.mark.parametrize("proxy", ["127.0.0.1"], indirect=True)
def test_a_garbage_header_falls_back_to_the_proxy(proxy):
    lock_out("unknown")
    assert login(PASSWORD) == 429
    assert login(PASSWORD, "10.0.0.2") == 200


@pytest.mark.parametrize("proxy", ["", "127.0.0.1"], indirect=True)
def test_a_proxied_client_is_not_local(proxy):
    """Companion's Discover answers only an IP or .local name: a refused body (415) means it counted as local."""
    time.sleep(2.1)  # one Discover per 2 s
    code, body = api("POST", "/api/config", b"{}")
    assert code == (403 if proxy else 415), body


@pytest.mark.parametrize("proxy", ["127.0.0.1"], indirect=True)
def test_a_challenge_is_bound_to_the_forwarded_client(proxy):
    a = Authenticator()
    a.id = b"x" * 16
    c = challenge(True, {"X-Forwarded-For": "10.0.0.1"})
    code, body, _ = a.login(challenge=c, headers={"X-Forwarded-For": "10.0.0.2"})
    assert code == 403 and "challenge" in body["error"]
    c = challenge(True, {"X-Forwarded-For": "10.0.0.1"})
    code, body, _ = a.login(challenge=c, headers={"X-Forwarded-For": "10.0.0.1"})
    assert code == 403 and body["error"] == "passkey not accepted"  # past the challenge: no such passkey


# Last: its wrong passwords count toward the 30 per minute that would lock the clients of the tests above.
@pytest.mark.parametrize("proxy", ["127.0.0.1"], indirect=True)
def test_ipv6_clients_behind_the_proxy_have_their_own_lockout(proxy):
    unlock("2001:db8:0:3::1")  # waits out the 30 per minute the tests above may have used up
    lock_out("2001:db8:0:1::1")
    assert login(PASSWORD, "2001:db8:0:2::1") == 200
    assert login(PASSWORD) == 200
    assert login(PASSWORD, "2001:db8:0:1::2") == 429  # one host may use any address of its /64
    unlock("2001:db8:0:1::1")
