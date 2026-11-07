"""The HTTPS certificate can be downloaded, and an own certificate is checked before it is used."""

import datetime
import hashlib
import json

import pytest
from conftest import TARGET, api, status
from cryptography import x509
from cryptography.hazmat.primitives import hashes, serialization
from cryptography.hazmat.primitives.asymmetric import ec
from cryptography.x509.oid import NameOID

pytestmark = pytest.mark.skipif(TARGET == "sim", reason="the simulator has no HTTPS")


def pair(cn: str) -> tuple[str, str]:
    key = ec.generate_private_key(ec.SECP256R1())
    name = x509.Name([x509.NameAttribute(NameOID.COMMON_NAME, cn)])
    now = datetime.datetime.now(datetime.UTC)
    cert = (
        x509.CertificateBuilder()
        .subject_name(name)
        .issuer_name(name)
        .public_key(key.public_key())
        .serial_number(x509.random_serial_number())
        .not_valid_before(now)
        .not_valid_after(now + datetime.timedelta(days=30))
        .sign(key, hashes.SHA256())
    )
    pem_key = key.private_bytes(
        serialization.Encoding.PEM, serialization.PrivateFormat.PKCS8, serialization.NoEncryption()
    )
    return cert.public_bytes(serialization.Encoding.PEM).decode(), pem_key.decode()


def test_the_downloaded_certificate_matches_the_fingerprint():
    code, pem = api("GET", "/api/cert", auth=False)
    assert code == 200 and b"BEGIN CERTIFICATE" in pem and b"PRIVATE KEY" not in pem
    der = x509.load_pem_x509_certificate(pem).public_bytes(serialization.Encoding.DER)
    fp = ":".join(f"{b:02X}" for b in hashlib.sha256(der).digest())
    assert fp == status()["https_fingerprint"]


def test_an_own_certificate_is_checked_without_being_applied():
    cert, key = pair("dash.example.org")
    code, body = api("PUT", "/api/tls", json.dumps({"cert": cert, "key": key}).encode())
    info = json.loads(body)
    assert code == 200 and info["subject"] == "CN=dash.example.org" and len(info["sha256"]) == 95, info
    assert status()["https_custom"] is False


def test_a_key_that_does_not_belong_to_the_certificate_is_refused():
    cert, _ = pair("a")
    _, other = pair("b")
    assert api("PUT", "/api/tls", json.dumps({"cert": cert, "key": other}).encode())[0] == 400
    assert api("PUT", "/api/tls", b'{"cert":"nope","key":"nope"}')[0] == 400
