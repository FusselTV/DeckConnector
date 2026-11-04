"""Encoding of the deckbridge protocol, see docs/protocol.md."""

import struct
from dataclasses import dataclass
from enum import IntEnum

IMAGE = 0x01
TIME = 0x02
ACK = 0x81
TIME_REPLY = 0x82
INPUT = 0x83

_IMAGE = struct.Struct("<BBHHHHI")
_TIME = struct.Struct("<B7xQ")
_ACK = struct.Struct("<BBBxIQQQ")
_TIME_REPLY = struct.Struct("<B7xQQ")
_INPUT = struct.Struct("<BBBBhHHHH10xQ")


class AckResult(IntEnum):
    SHOWN = 0
    SUPERSEDED = 1
    REJECTED_RECT = 2
    REJECTED_ROLE = 3
    NO_DEVICE = 4
    BUSY = 5


class InputKind(IntEnum):
    KEY_DOWN = 1
    KEY_UP = 2
    ENCODER_DOWN = 3
    ENCODER_UP = 4
    ENCODER_TURN = 5
    TOUCH_TAP = 6
    TOUCH_LONG = 7
    TOUCH_SWIPE = 8


@dataclass(frozen=True)
class Ack:
    result: AckResult
    display: int
    seq: int
    t_recv: int
    t_start: int
    t_done: int


@dataclass(frozen=True)
class Input:
    kind: InputKind
    index: int
    display: int
    value: int
    x: int
    y: int
    x2: int
    y2: int
    t: int


def image(display: int, x: int, y: int, w: int, h: int, seq: int, jpeg: bytes) -> bytes:
    return _IMAGE.pack(IMAGE, display, x, y, w, h, seq) + jpeg


def time_request(server_time_us: int) -> bytes:
    return _TIME.pack(TIME, server_time_us)


def parse(msg: bytes) -> Ack | Input | tuple[int, int]:
    kind = msg[0]
    if kind == ACK:
        _, result, display, seq, t_recv, t_start, t_done = _ACK.unpack(msg)
        return Ack(AckResult(result), display, seq, t_recv, t_start, t_done)
    if kind == INPUT:
        _, k, index, display, value, x, y, x2, y2, t = _INPUT.unpack(msg)
        return Input(InputKind(k), index, display, value, x, y, x2, y2, t)
    if kind == TIME_REPLY:
        _, server, bridge = _TIME_REPLY.unpack(msg)
        return server, bridge
    raise ValueError(f"unknown message type {kind:#x}")
