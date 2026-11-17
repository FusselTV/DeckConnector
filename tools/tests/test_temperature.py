"""The chip's temperature: in the status for integrations, and as a 10 minute history in the metrics."""

import json
import time

from conftest import api


def test_the_chip_temperature_is_in_status_and_metrics():
    time.sleep(11)  # the history starts with the second 5 s sample
    temp = json.loads(api("GET", "/api/status")[1]).get("temp_c")
    assert temp is not None and 20 <= temp <= 90, f"temp_c {temp}"
    series = json.loads(api("GET", "/api/metrics")[1])["temp"]
    assert series and all(20 <= t <= 90 for t in series if t), series[-5:]
