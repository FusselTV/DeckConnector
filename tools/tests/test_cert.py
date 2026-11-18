"""The board's own certificate comes from its device CA and passes what browsers and Apple check; an own certificate
is checked before it is used."""

import datetime
import hashlib
import json
import subprocess
from ipaddress import ip_address, ip_network
from pathlib import Path

import pytest
from conftest import api, status
from cryptography import x509
from cryptography.hazmat.primitives import hashes, serialization
from cryptography.hazmat.primitives.asymmetric import ec
from cryptography.x509.oid import ExtendedKeyUsageOID, NameOID
from cryptography.x509.verification import PolicyBuilder, Store, VerificationError


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


def fetch(path: str) -> bytes:
    code, body = api("GET", path)
    assert code == 200, body
    return body


def ca_like(real: x509.Certificate, key: ec.EllipticCurvePrivateKey) -> x509.Certificate:
    """A CA of our own with the name constraints of the board's: it can sign what a stolen CA key could."""
    now = datetime.datetime.now(datetime.UTC)
    return (
        x509.CertificateBuilder()
        .subject_name(real.subject)
        .issuer_name(real.subject)
        .public_key(key.public_key())
        .serial_number(x509.random_serial_number())
        .not_valid_before(now - datetime.timedelta(days=1))
        .not_valid_after(now + datetime.timedelta(days=30))
        .add_extension(x509.BasicConstraints(ca=True, path_length=0), critical=True)
        .add_extension(real.extensions.get_extension_for_class(x509.KeyUsage).value, critical=True)
        .add_extension(real.extensions.get_extension_for_class(x509.NameConstraints).value, critical=True)
        .add_extension(x509.SubjectKeyIdentifier.from_public_key(key.public_key()), critical=False)
        .sign(key, hashes.SHA256())
    )


def leaf_for(ca: x509.Certificate, ca_key: ec.EllipticCurvePrivateKey, san: x509.GeneralName) -> x509.Certificate:
    key = ec.generate_private_key(ec.SECP256R1())
    now = datetime.datetime.now(datetime.UTC)
    return (
        x509.CertificateBuilder()
        .subject_name(x509.Name([x509.NameAttribute(NameOID.COMMON_NAME, "forged")]))
        .issuer_name(ca.subject)
        .public_key(key.public_key())
        .serial_number(x509.random_serial_number())
        .not_valid_before(now - datetime.timedelta(days=1))
        .not_valid_after(now + datetime.timedelta(days=30))
        .add_extension(x509.SubjectAlternativeName([san]), critical=False)
        .add_extension(x509.ExtendedKeyUsage([ExtendedKeyUsageOID.SERVER_AUTH]), critical=False)
        .add_extension(x509.AuthorityKeyIdentifier.from_issuer_public_key(ca_key.public_key()), critical=False)
        .sign(ca_key, hashes.SHA256())
    )


def openssl_verifies(ca: x509.Certificate, leaf: x509.Certificate, check: list[str], tmp: Path) -> bool:
    (tmp / "ca.pem").write_bytes(ca.public_bytes(serialization.Encoding.PEM))
    (tmp / "leaf.pem").write_bytes(leaf.public_bytes(serialization.Encoding.PEM))
    run = ["openssl", "verify", "-x509_strict", "-purpose", "sslserver", *check, "-CAfile", "ca.pem", "leaf.pem"]
    return subprocess.run(run, cwd=tmp, capture_output=True).returncode == 0


@pytest.mark.sim
def test_the_device_ca_vouches_only_for_this_board(tmp_path):
    ca = x509.load_der_x509_certificate(fetch("/api/ca"))
    leaf = x509.load_pem_x509_certificate(fetch("/api/cert"))
    name = status()["name"] + ".local"
    assert ca.extensions.get_extension_for_class(x509.BasicConstraints).value.ca
    nc = ca.extensions.get_extension_for_class(x509.NameConstraints)
    assert nc.critical and nc.value.permitted_subtrees == [x509.DNSName(name), x509.DNSName("localhost")]
    assert nc.value.excluded_subtrees == [x509.IPAddress(ip_network("0.0.0.0/0")), x509.IPAddress(ip_network("::/0"))]
    assert ExtendedKeyUsageOID.SERVER_AUTH in leaf.extensions.get_extension_for_class(x509.ExtendedKeyUsage).value
    assert leaf.not_valid_after_utc - leaf.not_valid_before_utc <= datetime.timedelta(days=825)  # Apple's limit
    san = leaf.extensions.get_extension_for_class(x509.SubjectAlternativeName).value
    assert sorted(san.get_values_for_type(x509.DNSName)) == sorted([name, "localhost"])
    assert san.get_values_for_type(x509.IPAddress) == []
    for who in [name, "localhost"]:
        PolicyBuilder().store(Store([ca])).build_server_verifier(x509.DNSName(who)).verify(leaf, [])
        assert openssl_verifies(ca, leaf, ["-verify_hostname", who], tmp_path)

    key = ec.generate_private_key(ec.SECP256R1())
    stolen = ca_like(ca, key)
    own = leaf_for(stolen, key, x509.DNSName(name))
    PolicyBuilder().store(Store([stolen])).build_server_verifier(x509.DNSName(name)).verify(own, [])
    assert openssl_verifies(stolen, own, ["-verify_hostname", name], tmp_path)
    for san, check in [
        (x509.DNSName("nas.local"), ["-verify_hostname", "nas.local"]),
        (x509.DNSName("example.com"), ["-verify_hostname", "example.com"]),
        (x509.IPAddress(ip_address("192.168.1.1")), ["-verify_ip", "192.168.1.1"]),
        (x509.IPAddress(ip_address("fd00::1")), ["-verify_ip", "fd00::1"]),
    ]:
        forged = leaf_for(stolen, key, san)
        with pytest.raises(VerificationError):
            PolicyBuilder().store(Store([stolen])).build_server_verifier(san).verify(forged, [])
        assert not openssl_verifies(stolen, forged, check, tmp_path), san


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
