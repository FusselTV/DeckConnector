"""The coredump over the API. The crash itself needs a test build (idf.py -DDB_DEBUG_CRASH=1), see docs/api.md."""

import json
import time

import pytest
from conftest import api, status, wait_for_bridge


def test_a_dump_can_be_read_and_cleared():
    code, body = api("GET", "/api/coredump")
    assert code in (200, 404), body
    assert api("DELETE", "/api/coredump")[0] == 200
    assert api("GET", "/api/coredump")[0] == 404


def test_a_crash_leaves_a_dump_and_no_rollback():
    routes = json.loads(api("GET", "/api")[1])
    if not any(r["path"] == "/api/debug/crash" for r in routes):
        pytest.skip("not a DB_DEBUG_CRASH test build")
    before = status()["firmware"]
    api("DELETE", "/api/coredump")
    api("POST", "/api/debug/crash")
    time.sleep(5)
    wait_for_bridge(60)
    dump = json.loads(api("GET", "/api/coredump")[1])
    assert dump["task"] == "httpd" and dump["mcause"] == 7, dump  # the store to address 0
    assert status()["firmware"] == before, "a crash on a confirmed image must not roll back"
