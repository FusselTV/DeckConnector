"""The speaker, heard by the board's own microphone: Find plays its pitch, volume 0 and the amplifier off stay
silent."""

import json
import threading
import time

import pytest
from conftest import api


@pytest.fixture
def speaker():
    if not json.loads(api("GET", "/api/status")[1]).get("speaker"):
        pytest.skip("this board has no speaker")


def test_find_is_heard_at_its_pitch(speaker):
    code, body = api("POST", "/api/sound/selftest", b"{}", timeout=30)
    assert code == 200, body
    r = json.loads(body)
    print(f"\n{r}")
    assert r["dac"]["silent"] == 0 < r["dac"]["tone"], "the codec did not get Find, or not silence at volume 0"
    assert r["heard"], "the microphone does not hear Find over the controls"


def test_unknown_sound_and_volume_are_refused():
    assert api("POST", "/api/sound", json.dumps({"sound": "siren"}).encode())[0] == 400
    assert api("POST", "/api/sound", json.dumps({"sound": "find", "volume": 101}).encode())[0] == 400
    assert api("POST", "/api/sound", json.dumps({"sound": "ready", "volume": 0}).encode())[0] == 409
    for bad in (b'{"volume": 0}', b'{"gain": 43}', b'{"volume": "60"}', b"{", b"[]", b"x" * 64):
        assert api("POST", "/api/sound/selftest", bad)[0] == 400, bad


def put(body: dict) -> dict:
    code, reply = api("PUT", "/api/config", json.dumps(body).encode())
    assert code == 200, reply
    return json.loads(reply)


@pytest.mark.sim
def test_without_a_speaker_nothing_plays_and_the_status_says_so():
    was = json.loads(api("GET", "/api/config")[1])["speaker"]
    try:
        assert not put({"speaker": False}).get("restart")
        assert json.loads(api("GET", "/api/status")[1])["speaker"] is False
        for path in ("/api/sound", "/api/sound/selftest"):
            code, body = api("POST", path, b'{"sound": "find"}' if path == "/api/sound" else b"{}")
            assert code == 409 and "speaker" in json.loads(body)["error"], body
        assert not put({"speaker": True}).get("restart")
        assert json.loads(api("GET", "/api/status")[1])["speaker"] is True
        assert api("POST", "/api/sound", b'{"sound": "find"}')[0] == 200
        assert api("PUT", "/api/config", b'{"speaker": 1}')[0] == 400
    finally:
        put({"speaker": was})


def test_a_sound_does_not_end_a_stream(speaker):
    """The web UI plays a sound after each volume change; that once cut Home Assistant's music off."""
    result = {}
    t = threading.Thread(
        target=lambda: result.update(r=api("POST", "/api/sound/stream", b"\0" * 32000 * 3, timeout=30))
    )
    t.start()
    time.sleep(1)
    assert api("POST", "/api/sound", json.dumps({"sound": "connected", "volume": 5}).encode())[0] == 200
    t.join()
    code, body = result["r"]
    assert (code, json.loads(body)) == (200, {"ok": True, "stopped": False})
