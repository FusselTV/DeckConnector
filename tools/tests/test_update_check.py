"""The update check says which firmware runs. It has no releases to compare with yet."""

import json

import pytest
from conftest import api


@pytest.mark.sim
def test_the_update_check_names_the_running_firmware():
    code, body = api("GET", "/api/update/check")
    reply = json.loads(body)
    assert code == 200 and reply["current"] and reply["latest"] is None, body
