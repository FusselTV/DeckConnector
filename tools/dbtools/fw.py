"""Bench firmware loop over the network: no serial flashing, and A/B comparisons are a slot switch.

uv run python -m dbtools.fw deploy [--slot N] [--no-boot]   sign firmware/build/deckbridge.bin, upload, boot it
uv run python -m dbtools.fw use N                         boot slot N and wait until it answers
uv run python -m dbtools.fw build REV                     build REV in a cached worktree, keep the signed image
uv run python -m dbtools.fw ab REV_A REV_B                A into one bench slot and started, B into the other

Slots 2 and 3 are the bench slots, so the firmware in 0 and 1 stays as it is. Switching back from a firmware
older than the slots API needs a deploy, because it cannot boot a slot by choice. tools/flash.sh stays for
serial recovery.
"""

import argparse
import base64
import json
import os
import subprocess
import time
import urllib.error
import urllib.request
from pathlib import Path

from cryptography.hazmat.primitives import serialization

from dbtools import dbfw

ROOT = Path(__file__).resolve().parents[2]
CACHE = Path.home() / ".cache/deckbridge"
BENCH_SLOTS = (3, 2)
HOST = os.environ.get("DB_HOST", "192.168.1.214")
AUTH = (
    "Basic "
    + base64.b64encode(f"admin:{(Path.home() / '.config/deckbridge/admin_pass').read_text().strip()}".encode()).decode()
)


def api(method: str, path: str, body: bytes | None = None, timeout: float = 120):
    req = urllib.request.Request(
        f"http://{HOST}{path}", data=body, method=method, headers={"X-Deckbridge": "1", "Authorization": AUTH}
    )
    try:
        with urllib.request.urlopen(req, timeout=timeout) as r:
            return r.status, json.loads(r.read() or b"null")
    except urllib.error.HTTPError as e:
        return e.code, json.loads(e.read() or b"null")


def version_of(image: bytes) -> str:
    # esp_app_desc_t follows the 24 byte image header and an 8 byte segment header; version is at +16.
    return image[48:80].split(b"\0")[0].decode()


def sign(image: bytes) -> bytes:
    key = serialization.load_pem_private_key(dbfw.KEY.read_bytes(), None)
    return dbfw.build("p4", image, version_of(image), key)


def wait_version(version: str, secs: float = 90) -> float:
    """Seconds until a fresh start of `version` answers. A fresh uptime tells it from the firmware before."""
    start = time.time()
    while time.time() - start < secs:
        try:
            s = api("GET", "/api/status", timeout=2)[1]
            if s["firmware"] == version and s["uptime_s"] <= time.time() - start + 1:
                return time.time() - start
        except (OSError, KeyError, TypeError, json.JSONDecodeError):
            pass
        time.sleep(0.25)
    raise SystemExit(f"{version} did not come up within {secs:.0f} s")


def bench_slot(avoid: set) -> int:
    _, slots = api("GET", "/api/slots")
    return next(s for s in BENCH_SLOTS if s not in avoid and not slots[s]["running"])


def confirmed(secs: float = 150) -> None:
    """A firmware that has not yet confirmed itself refuses updates (rollback guard)."""
    start = time.time()
    while True:
        code, slots = api("GET", "/api/slots")
        if code == 200:
            if not any(s["running"] and s["state"] == "pending" for s in slots):
                break
        elif api("GET", "/api/status")[1]["uptime_s"] > 25:  # a firmware without slots: past its confirm time
            break
        if time.time() - start > secs:
            raise SystemExit("the running firmware never confirmed itself")
        time.sleep(1)
    if time.time() - start > 1:
        print(f"waited {time.time() - start:.0f} s for the running firmware to confirm itself")


def deploy(signed: bytes, slot: int, boot: bool) -> None:
    version = dbfw.parse(signed)["version"]
    confirmed()
    start = time.time()
    code, r = api("POST", f"/api/ota?slot={slot}&boot={int(boot)}", signed)
    if code != 200:
        raise SystemExit(f"upload into slot {slot} failed: {r}")
    print(f"{version} into slot {slot}: upload {time.time() - start:.1f} s", flush=True)
    if boot:
        print(f"booted in {wait_version(version):.1f} s")


def use(slot: int) -> None:
    confirmed()
    _, slots = api("GET", "/api/slots")
    code, r = api("POST", f"/api/slots/{slot}/boot")
    if code != 200:
        raise SystemExit(f"slot {slot} not booted: {r} (a firmware older than the slots API needs a deploy)")
    print(f"slot {slot} ({slots[slot]['version']}) up in {wait_version(slots[slot]['version']):.1f} s")


def build(rev: str) -> bytes:
    sha = subprocess.check_output(["git", "-C", ROOT, "rev-parse", rev], text=True).strip()
    out = CACHE / "fw" / f"{sha}.dbfw"
    if out.exists():
        print(f"{rev}: cached {out}")
        return out.read_bytes()
    wt = CACHE / "worktree"
    if wt.exists():
        subprocess.run(["git", "-C", wt, "checkout", "-q", "--detach", sha], check=True)
    else:
        subprocess.run(["git", "-C", ROOT, "worktree", "add", "-q", "--detach", wt, sha], check=True)
    start = time.time()
    r = subprocess.run(
        ["bash", "-c", ". ~/esp/esp-idf/export.sh >/dev/null 2>&1 && idf.py -C firmware build"],
        cwd=wt,
        capture_output=True,
        text=True,
    )
    if r.returncode:
        raise SystemExit(f"build of {rev} failed:\n{r.stdout[-3000:]}{r.stderr[-3000:]}")
    signed = sign((wt / "firmware/build/deckbridge.bin").read_bytes())
    out.parent.mkdir(parents=True, exist_ok=True)
    out.write_bytes(signed)
    print(f"{rev}: built in {time.time() - start:.0f} s, {out}")
    return signed


def main() -> None:
    ap = argparse.ArgumentParser()
    sub = ap.add_subparsers(dest="cmd", required=True)
    d = sub.add_parser("deploy")
    d.add_argument("--slot", type=int)
    d.add_argument("--no-boot", action="store_true")
    sub.add_parser("use").add_argument("slot", type=int)
    sub.add_parser("build").add_argument("rev")
    ab = sub.add_parser("ab")
    ab.add_argument("a")
    ab.add_argument("b")
    a = ap.parse_args()
    if a.cmd == "deploy":
        signed = sign((ROOT / "firmware/build/deckbridge.bin").read_bytes())
        deploy(signed, a.slot if a.slot is not None else bench_slot(set()), not a.no_boot)
    elif a.cmd == "use":
        use(a.slot)
    elif a.cmd == "build":
        build(a.rev)
    else:
        images = build(a.a), build(a.b)
        first = bench_slot(set())
        deploy(images[0], first, boot=True)
        second = bench_slot(set())  # the other bench slot, now that A runs
        deploy(images[1], second, boot=False)
        print(f"A runs in slot {first}, B waits in slot {second}: `use {second}` / `use {first}`")


if __name__ == "__main__":
    main()
