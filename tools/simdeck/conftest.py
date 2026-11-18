"""Every supported deck model through the firmware's own stack in firmware/simdeck. `uv run pytest simdeck`; one
simulator at a time. Pictures of each deck land in ~/.cache/simdeck/shots for a person to look at."""

import itertools
import time
from pathlib import Path

import pytest

from dbtools.simdeck import ELF, SimDeck

WORK = Path.home() / ".cache/simdeck/tests"
SHOTS = Path.home() / ".cache/simdeck/shots"
ELGATO = ["original", "mk2", "xl", "mini", "neo", "plus", "plus-xl", "pedal", "module6", "module15", "module32"]
MIRABOX = ["mirabox-293v3", "mirabox-n4"]
MODELS = ELGATO + MIRABOX
DISPLAY = [m for m in MODELS if m != "pedal"]
_ports = itertools.count(18700)


def pytest_collection_modifyitems(config, items):
    if not ELF.exists():
        for item in items:
            item.add_marker(pytest.mark.skip(reason=f"build {ELF.parent.parent} first"))


@pytest.fixture
def start(request):
    sims = []

    def start(*models: str, limit: bool = True, **env: str) -> SimDeck:
        name = request.node.name.replace("[", "-").rstrip("]")
        sim = SimDeck(list(models), next(_ports), WORK / name, limit=limit, **env).start()
        sims.append(sim)
        return sim

    yield start
    bad = []
    for sim in sims:
        if sim.proc.poll() is not None:
            bad.append(f"the simulator ended: {sim.log()[-2000:]}")
        bad += [v for v in sim.violations() if not any(x in v for x in getattr(sim, "tolerated", ()))]
        sim.stop()
    assert not bad, bad


def shot(sim: SimDeck, n: int, name: str):
    img, _ = sim.screen(n)
    SHOTS.mkdir(parents=True, exist_ok=True)
    if img.width:
        img.save(SHOTS / f"{name}.png")
    return img


def dismiss_prompt(sim: SimDeck, n: int = 0) -> None:
    """The simulator finds a Companion at once; "Nein" is the third key on every deck. The deck's picture
    changes when the prompt goes."""
    assert sim.wait(lambda: drawn_since(sim, n, 0), 5), "the deck shows nothing"
    time.sleep(0.3)
    since = time.time()
    sim.tap(n, 2)
    assert sim.wait(lambda: drawn_since(sim, n, since), 5), "the prompt did not go"


def drawn_since(sim: SimDeck, n: int, since: float) -> list[Path]:
    return [f for f in (sim.decks / str(n)).iterdir() if f.suffix in (".jpg", ".bmp") and f.stat().st_mtime >= since]
