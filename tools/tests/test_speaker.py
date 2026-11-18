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
    r = json.loads(body)
    print(f"\n{r}")
    assert code == 200
    assert r["dac"]["tone"] > 100 * max(r["dac"]["silent"], 1), "the codec did not get Find"
    assert r["heard"], "the microphone does not hear Find over the controls"


def test_unknown_sound_and_volume_are_refused():
    assert api("POST", "/api/sound", json.dumps({"sound": "siren"}).encode())[0] == 400
    assert api("POST", "/api/sound", json.dumps({"sound": "find", "volume": 101}).encode())[0] == 400
    assert api("POST", "/api/sound", json.dumps({"sound": "ready", "volume": 0}).encode())[0] == 409
    assert api("POST", "/api/sound/selftest", json.dumps({"volume": 0}).encode())[0] == 400
