"""Passkeys, with a software authenticator: registration, login and each check that must refuse a forged reply."""

import base64
import hashlib
import json
import os
import ssl
import urllib.request

import pytest
from conftest import HOST, PASSWORD, TARGET, status
from cryptography.hazmat.primitives import hashes, serialization
from cryptography.hazmat.primitives.asymmetric import ec

pytestmark = pytest.mark.sim

# Browsers allow passkeys over HTTPS by a name; the simulator has no HTTPS, and http://localhost counts as secure.
SCHEME = "http" if TARGET == "sim" else "https"
RP = "localhost" if TARGET == "sim" else f"{status()['name']}.local"
PORT = HOST.split(":")[1] if TARGET == "sim" else ""
ORIGIN = f"{SCHEME}://{RP}" + (f":{PORT}" if PORT else "")
TLS = ssl._create_unverified_context()


def b64u(b: bytes) -> str:
    return base64.urlsafe_b64encode(b).rstrip(b"=").decode()


def sha(b: bytes) -> bytes:
    return hashlib.sha256(b).digest()


def call(method: str, path: str, body: dict | None = None, host: str = "", admin: bool = False):
    """By IP, with the Host header a browser on the page's name would send."""
    req = urllib.request.Request(
        f"{SCHEME}://{HOST.split(':')[0] if SCHEME == 'https' else HOST}{path}",
        data=json.dumps(body).encode() if body is not None else None,
        method=method,
        headers={"X-DeckConnector": "1", "Host": host or ORIGIN.split("://")[1]},
    )
    if admin:
        req.add_header("Authorization", "Basic " + base64.b64encode(f"admin:{PASSWORD}".encode()).decode())
    try:
        with urllib.request.urlopen(req, timeout=10, context=TLS if SCHEME == "https" else None) as r:
            return r.status, json.loads(r.read() or b"null"), r.headers
    except urllib.error.HTTPError as e:
        return e.code, json.loads(e.read() or b"null"), e.headers


def challenge(login: bool) -> bytes:
    code, body, _ = call("POST", "/api/passkeys/challenge", {"login": login})
    assert code == 200, body
    return bytes.fromhex(body["challenge"])


class Authenticator:
    def __init__(self, uv: bool = True):
        self.key = ec.generate_private_key(ec.SECP256R1())
        self.id = os.urandom(16)
        self.uv = uv
        self.count = 0

    def auth_data(self, rp: str = RP, flags: int | None = None, new: bool = False) -> bytes:
        f = (0x01 | (0x04 if self.uv else 0) | (0x40 if new else 0)) if flags is None else flags
        ad = sha(rp.encode()) + bytes([f]) + self.count.to_bytes(4, "big")
        # the COSE key after the id is the browser's to read; the board takes the SPKI form
        return ad + bytes(16) + len(self.id).to_bytes(2, "big") + self.id + b"\xa5" if new else ad

    def register(self, name: str = "pytest", **over) -> tuple[int, dict]:
        client = json.dumps(
            {
                "type": "webauthn.create",
                "challenge": b64u(over.pop("challenge", None) or challenge(False)),
                "origin": ORIGIN,
            }
        ).encode()
        spki = self.key.public_key().public_bytes(
            serialization.Encoding.DER, serialization.PublicFormat.SubjectPublicKeyInfo
        )
        body = {
            "id": b64u(self.id),
            "clientDataJSON": b64u(client),
            "authenticatorData": b64u(self.auth_data(new=True)),
            "publicKey": b64u(spki),
            "publicKeyAlgorithm": -7,
            "name": name,
        } | over
        code, reply, _ = call("POST", "/api/passkeys", body, admin=True)
        return code, reply

    def login(self, step: bool = True, key=None, **over) -> tuple[int, dict, object]:
        """`over` replaces parts of the reply: type, challenge, origin, rp, flags, count."""
        self.count += step
        c = over.get("challenge") or challenge(True)
        client = json.dumps(
            {"type": over.get("type", "webauthn.get"), "challenge": b64u(c), "origin": over.get("origin", ORIGIN)}
        ).encode()
        ad = self.auth_data(over.get("rp", RP), over.get("flags"))
        sig = (key or self.key).sign(ad + sha(client), ec.ECDSA(hashes.SHA256()))
        body = {
            "id": b64u(self.id),
            "clientDataJSON": b64u(client),
            "authenticatorData": b64u(ad),
            "signature": b64u(sig),
        }
        return call("POST", "/api/login/passkey", body)


def passkeys() -> list[dict]:
    code, body, _ = call("GET", "/api/passkeys", admin=True)
    assert code == 200
    return body


@pytest.fixture
def key():
    a = Authenticator()
    code, reply = a.register()
    assert code == 200, reply
    a.slot = reply["id"]
    yield a
    for p in passkeys():
        call("DELETE", f"/api/passkeys/{p['id']}", admin=True)


def test_a_passkey_logs_in_and_can_be_renamed_and_removed(key):
    code, body, headers = key.login()
    assert code == 200 and body == {"ok": True}
    assert "_session=" in headers["Set-Cookie"]
    assert passkeys() == [{"id": key.slot, "name": "pytest", "rp": RP}]
    assert call("PUT", f"/api/passkeys/{key.slot}", {"name": "Laptop ✓"}, admin=True)[0] == 200
    assert passkeys()[0]["name"] == "Laptop ✓"
    assert call("PUT", f"/api/passkeys/{key.slot}", {"name": "x" * 24}, admin=True)[0] == 400
    assert call("DELETE", f"/api/passkeys/{key.slot}", admin=True)[0] == 200
    assert key.login()[0] == 403
    assert call("DELETE", f"/api/passkeys/{key.slot}", admin=True)[0] == 404


@pytest.mark.parametrize(
    "forged",
    [
        {"challenge": os.urandom(32)},
        {"type": "webauthn.create"},
        {"origin": "https://evil.example"},
        {"rp": "evil.example"},
        {"flags": 0x04},  # not present
        {"flags": 0x01},  # not verified, but made with verification
        {"step": False},  # the same counter again
        {"key": ec.generate_private_key(ec.SECP256R1())},
    ],
    ids=["challenge", "type", "origin", "rp_hash", "no_up", "no_uv", "counter", "signature"],
)
def test_a_forged_login_is_refused(key, forged):
    assert key.login()[0] == 200
    assert key.login(**forged)[0] == 403
    assert key.login()[0] == 200  # the passkey still works, and a good login resets the lockout count


def test_a_challenge_counts_once_and_for_its_kind(key):
    c = challenge(True)
    assert key.login(challenge=c)[0] == 200
    assert key.login(challenge=c)[0] == 403
    assert key.login(challenge=challenge(False))[0] == 403
    assert key.login()[0] == 200


def test_a_registration_is_checked():
    a = Authenticator()
    assert a.register(publicKeyAlgorithm=-8)[0] == 400
    p384 = ec.generate_private_key(ec.SECP384R1()).public_key()
    spki = p384.public_bytes(serialization.Encoding.DER, serialization.PublicFormat.SubjectPublicKeyInfo)
    assert a.register(publicKey=b64u(spki))[0] == 400
    assert a.register(name="")[0] == 400
    assert a.register(challenge=challenge(True))[0] == 400
    assert a.register(id=b64u(os.urandom(16)))[0] == 400
    assert passkeys() == []
    code, _, _ = call("POST", "/api/passkeys", {}, admin=False)
    assert code == 401


def test_eight_passkeys_at_most(key):
    for i in range(7):
        assert Authenticator().register(name=f"k{i}")[0] == 200
    assert Authenticator().register()[0] == 409


def test_passkeys_need_a_name_not_an_address():
    ip = HOST if TARGET == "sim" else HOST.split(":")[0]
    code, body, _ = call("POST", "/api/passkeys/challenge", {"login": True}, host=ip)
    assert code == 403 and "address" in body["error"]
