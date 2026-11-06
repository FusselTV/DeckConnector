"""Bootloader and partition table over the network: what is refused leaves both untouched, byte for byte."""

import hashlib
import json
import os
import struct
from pathlib import Path

import pytest
from conftest import api
from cryptography.hazmat.primitives import serialization

from dbtools import dbfw

BUILD = Path(__file__).resolve().parents[2] / "firmware/build"
KEY = serialization.load_pem_private_key(dbfw.KEY.read_bytes(), None)


def regions() -> tuple[str, str]:
    b = json.loads(api("GET", "/api/boot")[1])
    return b["bootloader"]["sha256"], b["ptable"]["sha256"]


def upload(image: bytes) -> int:
    return api("POST", "/api/ota?boot=0", image)[0]


def table(change) -> bytes:
    """Our partition table with one entry changed and a correct MD5, so only our own check can refuse it."""
    t = bytearray((BUILD / "partition_table/partition-table.bin").read_bytes())
    rows = [i for i in range(0, len(t), 32) if t[i : i + 2] == b"\xaa\x50"]
    change(t, rows)
    md5 = rows[-1] + 32
    t[md5 + 16 : md5 + 32] = hashlib.md5(t[: rows[-1] + 32]).digest()
    return bytes(t)


def shrink_nvs(t, rows):
    t[rows[0] + 8 : rows[0] + 12] = struct.pack("<I", 0x5000)


def move_running(t, rows):
    t[rows[3] + 4 : rows[3] + 8] = struct.pack("<I", 0x30000)  # ota_0 at 0x20000


@pytest.mark.parametrize(
    "image",
    [
        pytest.param(lambda: dbfw.build("ptable", table(shrink_nvs), "x", KEY), id="settings-change-size"),
        pytest.param(lambda: dbfw.build("ptable", table(move_running), "x", KEY), id="an-app-slot-overlaps"),
        pytest.param(lambda: dbfw.build("ptable", b"\xaa\x50" + bytes(3070), "x", KEY), id="table-without-md5"),
        pytest.param(lambda: dbfw.build("bootloader", b"\x00" * 4096, "x", KEY), id="not-a-bootloader"),
        pytest.param(lambda: dbfw.build("bootloader", b"\xe9" * 30000, "x", KEY), id="bootloader-too-big"),
        pytest.param(
            lambda: (
                dbfw.build("ptable", (BUILD / "partition_table/partition-table.bin").read_bytes(), "x", KEY)[:-1]
                + b"\0"
            ),
            id="changed-after-signing",
        ),
    ],
)
def test_refused_and_nothing_changes(image):
    before = regions()
    assert upload(image()) != 200
    assert regions() == before


@pytest.mark.bootwrite
# a -m on the command line replaces the addopts that deselect it: only this variable lets it write
@pytest.mark.skipif(not os.environ.get("DECKBRIDGE_BOOTWRITE"), reason="set DECKBRIDGE_BOOTWRITE=1 to write")
def test_the_same_bootloader_and_table_round_trip():
    before = regions()
    for name, target in (
        ("partition_table/partition-table.bin", "ptable"),
        ("bootloader/bootloader.bin", "bootloader"),
    ):
        assert upload(dbfw.build(target, (BUILD / name).read_bytes(), target, KEY)) == 200
    assert regions() == before
