import time

import pytest
from conftest import DISPLAY, MODELS, dismiss_prompt, shot

EXPECT = {  # model name, keys, encoders, touch strip
    "original": ("Stream Deck", 15, 0, False),
    "mk2": ("Stream Deck MK.2", 15, 0, False),
    "xl": ("Stream Deck XL", 32, 0, False),
    "mini": ("Stream Deck Mini", 6, 0, False),
    "neo": ("Stream Deck Neo", 10, 0, False),
    "plus": ("Stream Deck +", 8, 4, True),
    "plus-xl": ("Stream Deck + XL", 36, 6, True),
    "pedal": ("Stream Deck Pedal", 3, 0, False),
    "module6": ("Stream Deck 6 Module", 6, 0, False),
    "module15": ("Stream Deck 15 Module", 15, 0, False),
    "module32": ("Stream Deck 32 Module", 32, 0, False),
    "mirabox-293v3": ("Stream Dock 293V3", 15, 0, False),
    "mirabox-n4": ("Stream Dock N4", 14, 5, False),
}


def lit(img) -> int:
    return sum(1 for p in img.convert("L").get_flattened_data() if p > 40)


@pytest.mark.parametrize("model", MODELS)
def test_the_deck_comes_up_and_shows_the_ui(start, model):
    sim = start(model)
    name, keys, encoders, strip = EXPECT[model]
    deck = sim.status()["decks"][0]
    assert (deck["model"], deck["keys"], deck["encoders"]) == (name, keys, encoders)
    assert deck["dock"] == model.startswith(("original", "mk2", "xl", "mini", "neo", "plus", "pedal", "module"))
    if model == "pedal":
        code, body = sim.api("GET", f"/api/screen.jpg?deck={deck['serial']}")
        assert code == 404, body
        return
    _, desc = sim.screen(0)
    assert bool(desc["touch"]) == strip
    assert sim.wait(lambda: len(sim.shown(0)) >= 1), "the deck took no image"
    assert lit(shot(sim, 0, f"{model}-prompt")) > 500
    dismiss_prompt(sim)
    time.sleep(1)
    assert lit(shot(sim, 0, f"{model}-home")) > 500


def chord(sim, down: bool) -> None:
    c = sim.screen(0)[1]["menu_chord"]
    for i in (c["first"], c["last"]):
        if c["on"] == "encoders":
            sim.act(f"0 enc {i} {int(down)}")
        else:
            sim.key(0, i, down)


@pytest.mark.parametrize("model", DISPLAY)
def test_the_menu_chord_opens_the_menu(start, model):
    sim = start(model)
    dismiss_prompt(sim)
    chord(sim, True)
    time.sleep(2.4)
    chord(sim, False)
    assert sim.wait(lambda: sim.status()["menu_open"], 3), "the chord did not open the menu"
    time.sleep(0.5)
    shot(sim, 0, f"{model}-menu")


@pytest.mark.parametrize("model", ["mk2", "mini", "neo", "plus", "mirabox-n4"])
def test_the_screen_goes_dark_and_a_press_wakes_it(start, model):
    sim = start(model)
    dismiss_prompt(sim)
    sim.config(screen_off_s=10, setup_done=True)  # the home screen with its menu key, not the welcome
    bright = sim.decks / "0/brightness"
    assert sim.wait(lambda: bright.read_text() == "0", 15), "the deck stayed lit"
    keys = sim.screen(0)[1]["keys"]
    menu = sum(k["y"] == keys[0]["y"] for k in keys) - 1  # the home screen's menu key, last of the top row
    sim.tap(0, menu)
    assert sim.wait(lambda: bright.read_text() not in ("0", "-1"), 3), "a press did not wake it"
    time.sleep(0.5)
    assert not sim.status()["menu_open"], "the waking press opened the menu"


def test_the_screen_names_the_neo_info_bar(start):
    sim = start("neo")
    _, desc = sim.screen(0)
    assert not desc["touch"] and len(desc["strips"]) == 1, desc


def test_a_waiting_deck_stops_counting_as_dark_once_it_is_in(start):
    sim = start("mk2", "mini", "neo", limit=False)
    assert sim.wait(lambda: sim.status()["decks_over_limit"] == 1, 10)
    sim.config(max_decks=3)
    st = sim.wait(lambda: len((s := sim.status())["decks"]) == 3 and s, 10)
    assert st and st["decks_over_limit"] == 0, st and st["decks_over_limit"]
