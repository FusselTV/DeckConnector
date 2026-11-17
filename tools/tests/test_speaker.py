"""The speaker, heard through the bench camera's microphone: Find plays its pitch, a sound at volume 0 stays silent."""

import array
import json
import math
import subprocess
import threading
import time

import pytest
from conftest import api, get_config, put_config

from dbtools.site import CAMERA

RATE = 16000


def record(secs: float) -> array.array:
    """The camera's sound as 16-bit mono samples."""
    out = subprocess.run(
        [
            "ffmpeg",
            "-loglevel",
            "error",
            "-rtsp_transport",
            "tcp",
            "-i",
            CAMERA,
            "-t",
            str(secs),
            "-vn",
            "-ac",
            "1",
            "-ar",
            str(RATE),
            "-f",
            "s16le",
            "-",
        ],
        capture_output=True,
        timeout=secs + 20,
        check=True,
    ).stdout
    return array.array("h", out)


def power(samples, hz: float) -> float:
    """Goertzel: the strength of one pitch, per sample."""
    w = 2 * math.pi * hz / RATE
    c = 2 * math.cos(w)
    s1 = s2 = 0.0
    for x in samples:
        s1, s2 = x + c * s1 - s2, s1
    return (s1 * s1 + s2 * s2 - c * s1 * s2) / max(len(samples), 1)


def loudest_window(samples, hz: float, secs: float = 0.1) -> float:
    n = int(RATE * secs)
    return max(power(samples[i : i + n], hz) for i in range(0, len(samples) - n, n // 2))


def heard_while(play, secs: float = 4) -> array.array:
    got = {}
    t = threading.Thread(target=lambda: got.setdefault("s", record(secs)))
    t.start()
    time.sleep(1.0)  # the stream needs a moment before it carries sound
    play()
    t.join()
    return got["s"]


@pytest.fixture
def quiet():
    if not CAMERA:
        pytest.skip("needs the bench camera")
    if not json.loads(api("GET", "/api/status")[1]).get("speaker"):
        pytest.skip("this board has no speaker")
    before = get_config()["volume"]
    put_config({"volume": 0})
    yield
    put_config({"volume": before})


def test_find_is_heard_at_its_pitch(quiet):
    s = heard_while(lambda: api("POST", "/api/sound", json.dumps({"sound": "find"}).encode()))
    tone, off = loudest_window(s, 2093), loudest_window(s, 1500)
    print(f"\n2093 Hz {tone:.0f}, 1500 Hz {off:.0f}")
    assert tone > 20 * off, "Find's 2093 Hz is not in the camera's sound"


def test_volume_zero_plays_nothing_but_find_still_does(quiet):
    s = heard_while(lambda: api("POST", "/api/sound", json.dumps({"sound": "ready", "volume": 0}).encode()))
    silent = loudest_window(s, 1047)
    s = heard_while(lambda: api("POST", "/api/sound", json.dumps({"sound": "ready", "volume": 80}).encode()))
    loud = loudest_window(s, 1047)
    print(f"\n1047 Hz at 0: {silent:.0f}, at 80: {loud:.0f}")
    assert loud > 20 * silent


def test_unknown_sound_and_volume_are_refused():
    assert api("POST", "/api/sound", json.dumps({"sound": "siren"}).encode())[0] == 400
    assert api("POST", "/api/sound", json.dumps({"sound": "find", "volume": 101}).encode())[0] == 400
