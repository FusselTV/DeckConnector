"""Print the bridge's serial console over the RFC2217 bridge."""

import argparse
import sys
import time

import serial

URL = "rfc2217://192.168.1.170:4000?ign_set_control"


def main() -> None:
    ap = argparse.ArgumentParser()
    ap.add_argument("--secs", type=float, default=10)
    ap.add_argument("--reset", action="store_true")
    a = ap.parse_args()
    # RTS and DTR must be low before open, or the board stays in reset.
    s = serial.serial_for_url(URL, 115200, timeout=0.3, do_not_open=True)
    s.dtr = False
    s.rts = False
    s.open()
    if a.reset:
        s.rts = True
        time.sleep(0.2)
        s.rts = False
    end = time.time() + a.secs
    while time.time() < end:
        data = s.read(4096)
        if data:
            sys.stdout.write(data.decode(errors="replace"))
            sys.stdout.flush()


if __name__ == "__main__":
    main()
