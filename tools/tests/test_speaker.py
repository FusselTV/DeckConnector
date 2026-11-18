"""The speaker, heard by the board's own microphone: Find plays its pitch, volume 0 and the amplifier off stay
silent."""

import json

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
