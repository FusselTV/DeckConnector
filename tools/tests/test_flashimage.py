"""The browser flasher's files from firmware/build: every part at its offset, hashes that match."""

import hashlib

import pytest
from conftest import SIM

from dbtools import flashimage

BUILD = SIM.parents[3] / "firmware/build"
pytestmark = [
    pytest.mark.sim,
    pytest.mark.skipif(not (BUILD / "flasher_args.json").exists(), reason="no firmware build"),
]


def test_factory_and_update_parts(tmp_path):
    m = flashimage.build(BUILD, tmp_path)
    assert m["chip"] == "esp32p4" and m["chip_id"] == 18
    factory = (tmp_path / "deckconnector-factory.bin").read_bytes()
    for off, name in [
        (0x2000, "bootloader/bootloader.bin"),
        (0x8000, "partition_table/partition-table.bin"),
        (0xF000, "ota_data_initial.bin"),
        (0x20000, "deckconnector.bin"),
    ]:
        data = (BUILD / name).read_bytes()
        assert factory[off : off + len(data)] == data, name
    assert set(factory[0x9000:0xF000]) == {0xFF}  # NVS: a factory install starts with default settings
    for build in m["builds"].values():
        for p in build["parts"]:
            data = (tmp_path / p["path"]).read_bytes()
            assert len(data) == p["size"] and hashlib.sha256(data).hexdigest() == p["sha256"]
    assert [p["offset"] for p in m["builds"]["update"]["parts"]] == [0xF000, 0x20000]
