"""The board's own certificate comes from its device CA and passes what browsers and Apple check; an own certificate
is checked before it is used."""

import base64
import datetime
import hashlib
import json
import urllib.request
from ipaddress import ip_address

import pytest
from conftest import PASSWORD, TARGET, WORKER, api, start_sim, status
from cryptography import x509
from cryptography.hazmat.primitives import hashes, serialization
from cryptography.hazmat.primitives.asymmetric import ec
from cryptography.x509.oid import ExtendedKeyUsageOID, NameOID
from cryptography.x509.verification import PolicyBuilder, Store


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


def fetch(host: str, path: str) -> bytes:
    auth = base64.b64encode(f"admin:{PASSWORD}".encode()).decode()
    req = urllib.request.Request(f"http://{host}{path}", headers={"Authorization": f"Basic {auth}"})
    with urllib.request.urlopen(req, timeout=10) as r:
        return r.read()


def check_chain(host: str, ips: list[str]) -> None:
    ca = x509.load_der_x509_certificate(fetch(host, "/api/ca"))
    leaf = x509.load_pem_x509_certificate(fetch(host, "/api/cert"))
    name = json.loads(fetch(host, "/api/status"))["name"] + ".local"
    assert ca.extensions.get_extension_for_class(x509.BasicConstraints).value.ca
    assert ca.extensions.get_extension_for_class(x509.NameConstraints).critical
    assert ExtendedKeyUsageOID.SERVER_AUTH in leaf.extensions.get_extension_for_class(x509.ExtendedKeyUsage).value
    assert leaf.not_valid_after_utc - leaf.not_valid_before_utc <= datetime.timedelta(days=825)  # Apple's limit
    san = leaf.extensions.get_extension_for_class(x509.SubjectAlternativeName).value
    assert sorted(san.get_values_for_type(x509.DNSName)) == sorted([name, "localhost"])
    assert [str(i) for i in san.get_values_for_type(x509.IPAddress)] == ips
    for who in [x509.DNSName(name), x509.DNSName("localhost"), *(x509.IPAddress(ip_address(i)) for i in ips)]:
        PolicyBuilder().store(Store([ca])).build_server_verifier(who).verify(leaf, [])


@pytest.mark.sim
@pytest.mark.skipif(TARGET != "sim", reason="the simulator's LAN address")
def test_the_device_ca_issues_a_certificate_browsers_and_apple_accept():
    port = 28280 + WORKER
    sim = start_sim(port, DB_SIM_PASSWORD=PASSWORD, DB_SIM_LAN_IP="192.168.77.5")
    try:
        check_chain(f"127.0.0.1:{port}", ["192.168.77.5"])
    finally:
        sim.kill()
        sim.wait()


def test_the_downloaded_certificate_matches_the_fingerprint():
    code, pem = api("GET", "/api/cert", auth=False)
    assert code == 200 and b"BEGIN CERTIFICATE" in pem and b"PRIVATE KEY" not in pem
    der = x509.load_pem_x509_certificate(pem).public_bytes(serialization.Encoding.DER)
    fp = ":".join(f"{b:02X}" for b in hashlib.sha256(der).digest())
    assert fp == status()["https_fingerprint"]


@pytest.mark.parametrize("cn", ["dash.example.org", 'Foo, "Inc."'])
def test_an_own_certificate_is_checked_without_being_applied(cn):
    cert, key = pair(cn)
    code, body = api("PUT", "/api/tls", json.dumps({"cert": cert, "key": key}).encode())
    info = json.loads(body)
    assert code == 200 and info["subject"].startswith("CN=") and len(info["sha256"]) == 95, info
    assert status()["https_custom"] is False


def test_a_key_that_does_not_belong_to_the_certificate_is_refused():
    cert, _ = pair("a")
    _, other = pair("b")
    assert api("PUT", "/api/tls", json.dumps({"cert": cert, "key": other}).encode())[0] == 400
    assert api("PUT", "/api/tls", b'{"cert":"nope","key":"nope"}')[0] == 400


def test_a_certificate_that_would_crowd_out_the_settings_is_refused():
    cert, key = pair("big.example.org")
    chain = cert * 14  # a long chain: more than the 6 KB that may sit next to the settings in NVS
    assert len(chain) + len(key) > 6144
    code, body = api("PUT", "/api/tls", json.dumps({"cert": chain, "key": key}).encode())  # never "apply" on the bench
    assert code == 413, body
    assert status()["https_custom"] is False
