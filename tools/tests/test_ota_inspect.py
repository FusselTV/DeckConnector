"""The header of a .dbfw file says what it is before the upload, and whether the bridge would take it."""

import json

import pytest
from conftest import TARGET, api
from cryptography.hazmat.primitives import serialization
from cryptography.hazmat.primitives.asymmetric import ec

from dbtools import dbfw


def inspect(key) -> dict:
    head = dbfw.build("p4", b"\xe9" * 1000, "v9.8.7", key)[: dbfw.HEADER_LEN]
    code, body = api("POST", "/api/ota/inspect", head)
    assert code == 200, body
    return json.loads(body)


@pytest.mark.sim
def test_a_foreign_signature_is_named_before_the_upload():
    r = inspect(ec.generate_private_key(ec.SECP256R1()))
    assert r["target"] == "p4" and r["version"] == "v9.8.7" and r["length"] == 1000 and len(r["sha256"]) == 64
    assert not r["ok"] and r["error"], r


@pytest.mark.sim
def test_a_junk_file_gets_a_whole_reply():
    code, body = api("POST", "/api/ota/inspect", b"\x07" * dbfw.HEADER_LEN)
    r = json.loads(body)
    assert code == 200 and r["code"] == "ota_not_dbfw" and "version" not in r, r


@pytest.mark.sim
def test_a_short_header_is_refused():
    assert api("POST", "/api/ota/inspect", b"DBFW")[0] == 400


@pytest.mark.skipif(TARGET == "sim" or not dbfw.KEY.exists(), reason="needs the bridge and the signing key")
def test_an_own_file_passes():
    assert inspect(serialization.load_pem_private_key(dbfw.KEY.read_bytes(), None))["ok"]
