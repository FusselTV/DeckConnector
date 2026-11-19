"""components/db_matter on the firmware's own stack. No Matter stack runs: firmware/simdeck/components/esp_matter
stands in for esp-matter and writes what a controller would see to DB_SIM_MATTER_LOG, one line per endpoint,
attribute, label and event."""

import time
from collections import defaultdict
from pathlib import Path

from conftest import dismiss_prompt

from dbtools.simdeck import ELF

TABLE = ELF.parent / "partition_table/partition-table.bin"
NODE = 320  # 64 * (1 + gen 1 * 4 + slot 0): the first deck in a slot moves it to gen 1


def flash(work: Path) -> Path:
    """A flash image that keeps NVS, the Matter partition included, over a restart."""
    work.mkdir(parents=True, exist_ok=True)
    image = bytearray(b"\xff" * 0x400000)
    table = TABLE.read_bytes()
    image[0x8000 : 0x8000 + len(table)] = table
    path = work / "flash.bin"
    path.write_bytes(image)
    return path


class Controller:
    """DB_SIM_MATTER_LOG read as a controller keeps it: endpoints with their attributes, and the events."""

    def __init__(self, path: Path):
        self.path, self.read = path, 0

    def lines(self) -> list[tuple[str, int, str]]:
        """(kind, endpoint, the rest) for every line about an endpoint."""
        text = self.path.read_text() if self.path.exists() else ""
        out = []
        for line in text.splitlines():
            kind, *f = line.split(" ", 2)
            if f and f[0].isdigit():
                out.append((kind, int(f[0]), f[1] if len(f) > 1 else ""))
        return out

    def endpoints(self) -> dict[int, dict]:
        eps: dict[int, dict] = defaultdict(dict)
        for kind, ep, rest in self.lines():
            if kind == "endpoint":
                type_, _, parent = rest.split(" ")
                eps[ep].update(type=type_, parent=int(parent))
            elif kind == "attr":
                name, _, value = rest.partition(" ")
                eps[ep][name] = value
            elif kind == "label":
                eps[ep]["label"] = rest.split("=", 1)[1]
            elif kind == "tag":
                ns, tag, label = rest.split(" ", 2)
                eps[ep]["tag"] = (int(ns), int(tag), label)
            elif kind == "gone":
                eps.pop(ep, None)
        return eps

    def events(self) -> list[tuple[int, str]]:
        """Events since the last call."""
        lines = self.lines()
        new, self.read = lines[self.read :], len(lines)
        return [(ep, rest) for kind, ep, rest in new if kind == "event"]

    def wait_events(self, sim, n: int, secs: float = 5) -> list[tuple[int, str]]:
        got: list[tuple[int, str]] = []
        sim.wait(lambda: got.extend(self.events()) or len(got) >= n, secs)
        return got


def switches(ctl: Controller, node: int) -> dict[int, dict]:
    return {ep: e for ep, e in ctl.endpoints().items() if e.get("parent") == node}


def matter_on(start, log: Path, *models: str, count: int = 26, **env: str):
    """Matter turned on at run time; `count` switches in all, which come under the stack lock after the nodes."""
    sim = start(*models, DB_SIM_MATTER_LOG=str(log), **env)
    dismiss_prompt(sim)  # a press while a prompt covers the deck goes to the prompt, not to Matter
    ctl = Controller(log)
    sim.config(matter=True)
    assert sim.wait(lambda: sim.status()["matter"]["running"]), sim.log()
    n = lambda: sum(e.get("type") == "generic_switch" and "label" in e for e in ctl.endpoints().values())  # noqa: E731
    assert sim.wait(lambda: n() == count), (n(), ctl.lines()[-5:])
    return sim, ctl


def test_a_deck_is_one_bridged_node_with_a_labelled_switch_per_input(start, tmp_path):
    sim, ctl = matter_on(start, tmp_path / "m.log", "plus")
    eps = ctl.endpoints()
    assert eps[1]["type"] == "aggregator"
    node = eps[NODE]
    assert (node["type"], node["parent"], node["node_label"], node["product_name"]) == (
        "bridged_node",
        1,
        "Stream Deck +",
        "Stream Deck +",
    )
    assert node["serial_number"] == node["unique_id"] == sim.serial(0)
    assert (node["vendor_id"], node["product_id"], node["reachable"]) == ("65521", "32768", "1")

    sw = switches(ctl, NODE)
    assert len(sw) == 26, sorted(sw)  # 8 keys, 4 dial presses, 8 turns, 4 strip zones, 2 swipes
    assert all(e["type"] == "generic_switch" and e["features"] == "14" for e in sw.values())
    assert all(e["identify_type"] == "0" for e in sw.values()), "HA would add an Identify button"
    assert all(e["label"] == e["tag"][2] for e in sw.values()), "FixedLabel and TagList name it alike"
    assert [sw[NODE + 1 + k]["label"] for k in range(8)] == [f"Taste {k}" for k in range(1, 9)]
    assert [sw[NODE + 1 + k]["tag"][:2] for k in range(8)] == [(7, k) for k in range(1, 9)]
    assert sw[NODE + 49]["label"] == "Drehregler 1"
    assert {sw[ep]["label"] for ep in range(NODE + 9, NODE + 23)} >= {"Regler 1 links", "Touchfeld 4", "Wischen rechts"}


def test_presses_at_the_deck_reach_the_controller_and_api_presses_do_not(start, tmp_path):
    sim, ctl = matter_on(start, tmp_path / "m.log", "plus")
    ctl.events()

    sim.tap(0, 0)
    assert ctl.wait_events(sim, 2) == [(NODE + 1, "initial_press"), (NODE + 1, "short_release")]
    positions = [r for k, ep, r in ctl.lines() if k == "attr" and ep == NODE + 1 and r.startswith("position")]
    assert positions == ["position 1", "position 0"]
    sim.key(0, 1, True)
    time.sleep(1)
    sim.key(0, 1, False)
    assert ctl.wait_events(sim, 3) == [(NODE + 2, "initial_press"), (NODE + 2, "long_press"), (NODE + 2, "long_release")]
    sim.act("0 enc 0 1")
    sim.act("0 enc 0 0")
    assert ctl.wait_events(sim, 2) == [(NODE + 49, "initial_press"), (NODE + 49, "short_release")]
    right = next(ep for ep, e in switches(ctl, NODE).items() if e["label"] == "Regler 1 rechts")
    sim.act("0 turn 0 1")
    assert ctl.wait_events(sim, 2) == [(right, "initial_press"), (right, "short_release")]

    assert sim.api("POST", "/api/input", {"key": 3})[0] == 200
    assert not sim.status()["menu_open"]
    # a barrier: whatever the API press caused would come before these. POST /api/matter/press is the board
    # checklist's press for Matter alone.
    assert sim.api("POST", "/api/matter/press", {"key": 7})[0] == 200
    assert ctl.wait_events(sim, 2) == [(NODE + 8, "initial_press"), (NODE + 8, "short_release")], (
        "a press from the API reached Matter"
    )


def test_ids_stay_over_a_restart_and_an_unplugged_deck_turns_unreachable(start, tmp_path):
    image = flash(tmp_path)
    log = tmp_path / "m.log"
    sim, ctl = matter_on(start, log, "plus", DB_SIM_FLASH=str(image))
    before = {ep: (e["type"], e.get("label")) for ep, e in ctl.endpoints().items()}
    assert len(before) == 28, sorted(before)  # aggregator, node, 26 switches
    sim.act("0 unplug")
    assert sim.wait(lambda: ctl.endpoints()[NODE]["reachable"] == "0"), ctl.lines()[-5:]
    sim.stop()

    log.unlink()
    ctl = Controller(log)
    sim.start()  # Matter stays on; the deck is plugged in again at the start
    assert sim.wait(lambda: ctl.endpoints().get(NODE, {}).get("reachable") == "1"), ctl.lines()[-5:]
    assert sim.wait(lambda: {ep: (e["type"], e.get("label")) for ep, e in ctl.endpoints().items()} == before)
    reach = [rest for kind, ep, rest in ctl.lines() if kind == "attr" and ep == NODE and rest.startswith("reachable")]
    assert reach[0] == "reachable 0", "a known deck comes back unreachable until it is there"
    sim.tap(0, 0)
    assert ctl.wait_events(sim, 2) == [(NODE + 1, "initial_press"), (NODE + 1, "short_release")]


def test_another_deck_in_the_slot_gets_new_ids(start, tmp_path):
    """A controller keeps a device per id: a new deck in the same slot must not take over the old one's."""
    image = flash(tmp_path)
    log = tmp_path / "m.log"
    sim, _ = matter_on(start, log, "plus", DB_SIM_FLASH=str(image))
    sim.stop()
    log.unlink()
    ctl = Controller(log)
    sim.models = ["mini"]
    sim.start()
    new = NODE + 4 * 64  # gen 2
    assert sim.wait(lambda: len(switches(ctl, new)) == 6), ctl.lines()[-5:]
    eps = ctl.endpoints()
    assert eps[new]["node_label"] == "Stream Deck Mini"
    assert NODE not in eps and not switches(ctl, NODE), "the old deck's endpoints stayed"


def test_off_takes_effect_with_the_restart(start, tmp_path):
    image = flash(tmp_path)
    log = tmp_path / "m.log"
    sim, ctl = matter_on(start, log, "plus", DB_SIM_FLASH=str(image))
    sim.config(matter=False)
    assert sim.status()["matter"] == {"on": False, "running": True, "controllers": 0, "pairing": True}
    sim.stop()
    log.unlink()
    sim.start()
    assert sim.status()["matter"]["running"] is False
    assert sim.api("POST", "/api/matter/press", {"key": 0})[0] == 409
    assert not log.exists(), "the stack started although Matter is off"


def test_a_second_deck_numbers_both_names(start, tmp_path):
    _, ctl = matter_on(start, tmp_path / "m.log", "plus", "mini", count=26 + 6)
    nodes = {ep: e["node_label"] for ep, e in ctl.endpoints().items() if e.get("type") == "bridged_node"}
    assert sorted(nodes.values()) == ["Stream Deck + 1", "Stream Deck Mini 2"], nodes
    mini = next(ep for ep, label in nodes.items() if label.startswith("Stream Deck Mini"))
    assert len(switches(ctl, mini)) == 6
