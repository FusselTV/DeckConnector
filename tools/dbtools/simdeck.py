"""firmware/simdeck: the firmware's own stack on emulated decks. Start one with a mix of models, act as a person
at the decks through its control FIFO, and read back what the decks show."""

import atexit
import base64
import io
import json
import os
import subprocess
import time
import urllib.error
import urllib.request
from pathlib import Path

from PIL import Image

ELF = Path(__file__).resolve().parents[2] / "firmware/simdeck/build/deckbridge_simdeck.elf"

MODELS = {
    "original": "0fd9:006d",
    "mk2": "0fd9:0080",
    "xl": "0fd9:008f",
    "mini": "0fd9:0090",
    "neo": "0fd9:009a",
    "plus": "0fd9:0084",
    "plus-xl": "0fd9:00c6",
    "pedal": "0fd9:0086",
    "module6": "0fd9:00b8",
    "module15": "0fd9:00b9",
    "module32": "0fd9:00ba",
    "mirabox-293v3": "6603:1005",
    "mirabox-n4": "6602:1001",
}


class SimDeck:
    def __init__(
        self, models: list[str], port: int, work: Path, password: str = "simdeck-pass", limit: bool = True, **env: str
    ):
        """`limit`: raise max_decks (2 by default) to the number of models once the simulator is up."""
        self.models, self.port, self.work, self.password, self.limit, self.env = (
            models,
            port,
            work,
            password,
            limit,
            env,
        )
        self.decks = work / "decks"
        self.proc: subprocess.Popen | None = None

    def start(self) -> "SimDeck":
        try:
            urllib.request.urlopen(f"http://127.0.0.1:{self.port}/", timeout=1)
            raise RuntimeError(f"port {self.port} already answers: an earlier simulator still runs")
        except OSError:
            pass
        self.decks.mkdir(parents=True, exist_ok=True)
        for f in self.decks.glob("*/*"):
            f.unlink()
        env = {
            **os.environ,
            "DB_SIM_PORT": str(self.port),
            "DB_SIM_PASSWORD": self.password,
            "DB_SIM_DECK_MODELS": ",".join(m if ":" in m else MODELS[m] for m in self.models),
            "DB_SIM_DECK_DIR": str(self.decks),
            **self.env,
        }
        self.log_path = self.work / "sim.log"
        log = self.log_path.open("wb")
        self.proc = subprocess.Popen([ELF], env=env, stdout=log, stderr=subprocess.STDOUT)
        atexit.register(self.proc.kill)
        end = time.monotonic() + 10
        while time.monotonic() < end:
            try:
                urllib.request.urlopen(f"http://127.0.0.1:{self.port}/", timeout=1)
                if self.limit and len(self.models) > 2:  # 2 by default; the waiting decks come at once
                    self.config(max_decks=len(self.models))
                    assert self.wait(lambda: len(self.status()["decks"]) == len(self.models)), self.log()
                return self
            except OSError:
                if self.proc.poll() is not None:
                    break
                time.sleep(0.05)
        self.stop()
        raise RuntimeError(f"simdeck did not start:\n{self.log()}")

    def stop(self) -> None:
        if self.proc:
            self.proc.kill()
            self.proc.wait()
            self.proc = None

    def log(self) -> str:
        return self.log_path.read_text(errors="replace")

    def violations(self) -> list[str]:
        """What the emulated decks found wrong in the reports the drivers sent."""
        return [line for line in self.log().splitlines() if line.startswith(("deck_emu ", "mirabox_emu ", "sim_usb "))]

    def api(self, method: str, path: str, body: dict | bytes | None = None, timeout: float = 10):
        data = json.dumps(body).encode() if isinstance(body, dict) else body
        req = urllib.request.Request(
            f"http://127.0.0.1:{self.port}{path}", data=data, method=method, headers={"X-DeckConnector": "1"}
        )
        auth = base64.b64encode(f"admin:{self.password}".encode()).decode()
        req.add_header("Authorization", f"Basic {auth}")
        try:
            with urllib.request.urlopen(req, timeout=timeout) as r:
                return r.status, r.read()
        except urllib.error.HTTPError as e:
            return e.code, e.read()

    def status(self) -> dict:
        return json.loads(self.api("GET", "/api/status")[1])

    def config(self, **values) -> None:
        code, body = self.api("PUT", "/api/config", values)
        assert code == 200, body

    def act(self, *lines: str) -> None:
        """Lines for the control FIFO, see firmware/simdeck/components/sim_usb/usb_port.c."""
        assert self.proc and self.proc.poll() is None, f"the simulator has ended:\n{self.log()}"
        fd = os.open(self.decks / "ctl", os.O_WRONLY | os.O_NONBLOCK)  # fails at once without a reader
        try:
            os.write(fd, "".join(f"{line}\n" for line in lines).encode())
        finally:
            os.close(fd)

    def key(self, n: int, key: int, down: bool) -> None:
        """Key `key` as the firmware numbers it. A Mirabox deck takes it at its row and column, worked out from
        where /api/screen puts the key, since its input ids follow the panel, not the firmware's numbering."""
        if self.status()["decks"][n]["model"].startswith("Stream Deck"):
            self.act(f"{n} key {key} {int(down)}")
            return
        keys = json.loads(self.api("GET", f"/api/screen?deck={self.serial(n)}")[1])["keys"]
        w, h = max(k["w"] for k in keys), max(k["h"] for k in keys)
        self.act(f"{n} rc {keys[key]['y'] // h} {keys[key]['x'] // w} {int(down)}")

    def tap(self, n: int, key: int, hold: float = 0.05) -> None:
        self.key(n, key, True)
        time.sleep(hold)
        self.key(n, key, False)

    def serial(self, n: int) -> str:
        return self.status()["decks"][n]["serial"]

    def screen(self, n: int) -> tuple[Image.Image, dict]:
        """Deck n's picture as /api/screen describes it, and that description. A host still drawing replaces
        images between the description and their download; then the whole read starts again."""
        for _ in range(50):
            code, body = self.api("GET", f"/api/screen?deck={self.serial(n)}")
            assert code == 200, body
            desc = json.loads(body)
            img = Image.new("RGB", (desc["width"], desc["height"]))
            for shot in desc["images"]:
                code, jpg = self.api("GET", shot["url"])
                if b"no longer shown" in jpg:
                    break
                assert code == 200, (shot, jpg)
                img.paste(Image.open(io.BytesIO(jpg)).convert("RGB"), (shot["x"], shot["y"]))
            else:
                return img, desc
            time.sleep(0.05)
        raise AssertionError(f"deck {n} kept changing while its picture was read")

    def shown(self, n: int) -> dict[str, Image.Image]:
        """What deck n took over USB, by file name, turned as it was sent."""
        return {f.name: Image.open(f).convert("RGB") for f in sorted((self.decks / str(n)).glob("*.*"))}

    def wait(self, cond, secs: float = 10, step: float = 0.1):
        end = time.monotonic() + secs
        while time.monotonic() < end:
            got = cond()
            if got:
                return got
            time.sleep(step)
        return cond()
