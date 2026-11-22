"""Encoding of the surface protocol v2, see https://deckconnector.com/docs/own-server."""

import struct
from dataclasses import dataclass
from enum import IntEnum

import cbor2

ROLE, PUT, EVICT, FRAME, STREAM, FALLBACK, BRIGHTNESS, TIME, REBOOT, PAIRED = range(0x01, 0x0B)
HELLO, SHOWN, CREDIT, INPUT, DECK_ADDED, DECK_REMOVED, STATE, ERROR = range(0x80, 0x88)
BOARD = 0xFF
JPEG, BMP, PNG = 0, 1, 2
ALL = 0xFFFF

_HDR = struct.Struct("<BBH")
_SHOWN = struct.Struct("<BII")
_CREDIT = struct.Struct("<BIB")
_INPUT = struct.Struct("<IQBBhHHHH")


class InputKind(IntEnum):
    KEY_DOWN = 0
    KEY_UP = 1
    ENCODER_TURN = 2
    ENCODER_DOWN = 3
    ENCODER_UP = 4
    TOUCH_TAP = 5
    TOUCH_LONG = 6
    TOUCH_SWIPE = 7


@dataclass(frozen=True)
class Input:
    deck: int
    seq: int
    usb_time_us: int
    kind: InputKind
    index: int
    value: int
    x: int
    y: int
    x2: int
    y2: int


@dataclass(frozen=True)
class Shown:
    deck: int
    stream: bool
    id: int
    us: int


@dataclass(frozen=True)
class Credit:
    deck: int
    display: int
    bytes: int
    frames: int


@dataclass(frozen=True)
class Message:
    """A CBOR message (HELLO, DECK_ADDED, STATE, ERROR) or DECK_REMOVED."""

    type: int
    deck: int
    body: dict


def msg(kind: int, deck: int, payload: bytes = b"") -> bytes:
    return _HDR.pack(kind, deck, 0) + payload


def role(active: bool) -> bytes:
    return msg(ROLE, BOARD, bytes([int(active)]))


def put(slot: int, image: bytes, w: int, h: int, fmt: int = JPEG) -> bytes:
    return msg(PUT, BOARD, struct.pack("<HBHH", slot, fmt, w, h) + image)


def evict(slots: list[int] | None = None) -> bytes:
    """None: every slot."""
    if slots is None:
        return msg(EVICT, BOARD, struct.pack("<H", ALL))
    return msg(EVICT, BOARD, struct.pack(f"<H{len(slots)}H", len(slots), *slots))


def _pairs(shots: list[tuple[int, int]]) -> bytes:
    return struct.pack("<H", len(shots)) + b"".join(struct.pack("<BH", t, s) for t, s in shots)


def frame(deck: int, frame_id: int, shots: list[tuple[int, int]]) -> bytes:
    """`shots` are (target, slot)."""
    return msg(FRAME, deck, struct.pack("<I", frame_id) + _pairs(shots))


def stream(deck: int, target: int, seq: int, image: bytes, fmt: int = JPEG) -> bytes:
    return msg(STREAM, deck, struct.pack("<BIB", target, seq, fmt) + image)


def fallback(deck: int, shots: list[tuple[int, int]]) -> bytes:
    return msg(FALLBACK, deck, _pairs(shots))


def brightness(pct: int, deck: int = BOARD) -> bytes:
    return msg(BRIGHTNESS, deck, bytes([pct]))


def time_ms(unix_ms: int) -> bytes:
    return msg(TIME, BOARD, struct.pack("<Q", unix_ms))


def paired(token: str, code: str) -> bytes:
    return msg(PAIRED, BOARD, cbor2.dumps({"token": token, "code": code}))


def parse(data: bytes) -> Input | Shown | Credit | Message | None:
    """None for a type this host does not know."""
    kind, deck, _ = _HDR.unpack_from(data)
    body = data[_HDR.size :]
    if kind == INPUT:
        seq, t, k, index, value, x, y, x2, y2 = _INPUT.unpack(body)
        return Input(deck, seq, t, InputKind(k), index, value, x, y, x2, y2)
    if kind == SHOWN:
        stream_, ident, us = _SHOWN.unpack(body)
        return Shown(deck, bool(stream_), ident, us)
    if kind == CREDIT:
        return Credit(deck, *_CREDIT.unpack(body))
    if kind in (HELLO, DECK_ADDED, STATE, ERROR):
        return Message(kind, deck, cbor2.loads(body))
    if kind == DECK_REMOVED and not body:
        return Message(kind, deck, {})
    if kind == DECK_REMOVED:
        raise ValueError("DECK_REMOVED with a body")
    return None
