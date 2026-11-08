"""/tmp/deckbridge-bench.lock: one config change, reboot or device test of the bench bridge at a time."""

import contextlib
import fcntl
import os
import sys

LOCK = "/tmp/deckbridge-bench.lock"


def _held_by_ancestor(f) -> bool:
    """`flock LOCK cmd` or a test run that starts us already holds it: taking it again would wait forever."""
    ino = os.fstat(f.fileno()).st_ino
    with open("/proc/locks") as locks:
        holders = {int(p[4]) for p in map(str.split, locks) if p[1] == "FLOCK" and p[5].endswith(f":{ino}")}
    pid = os.getppid()
    while pid > 1:
        if pid in holders:
            return True
        with open(f"/proc/{pid}/stat") as st:
            pid = int(st.read().rsplit(")", 1)[1].split()[1])
    return False


@contextlib.contextmanager
def bench_lock():
    with open(LOCK, "a") as f:
        try:
            fcntl.flock(f, fcntl.LOCK_EX | fcntl.LOCK_NB)
        except BlockingIOError:
            if not _held_by_ancestor(f):
                print(f"waiting for {LOCK}", file=sys.stderr)
                fcntl.flock(f, fcntl.LOCK_EX)
        yield
