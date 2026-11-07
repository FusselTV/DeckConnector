"""The wizard's step lives on the bridge, so a reload or a second browser goes on where the first one was."""

import json

import pytest
from conftest import api, status


def step(n) -> int:
    return api("PUT", "/api/setup", json.dumps({"step": n}).encode())[0]


@pytest.mark.sim
def test_the_setup_step_is_kept_and_cleared():
    try:
        assert step(3) == 200
        s = status()["setup"]
        assert s["step"] == 3 and s["active"], s
    finally:
        assert step(0) == 200
    assert status()["setup"]["step"] == 0


@pytest.mark.sim
@pytest.mark.parametrize("n", [-1, 7, "2", None])
def test_a_bad_setup_step_is_refused(n):
    assert step(n) == 400
