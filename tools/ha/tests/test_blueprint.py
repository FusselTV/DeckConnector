from pathlib import Path
import shutil

from homeassistant.setup import async_setup_component
from pytest_homeassistant_custom_component.common import async_mock_service

from conftest import PLUS
from test_setup import setup

BLUEPRINT = Path(__file__).resolve().parents[3] / "blueprints/automation/deckconnector/dial_light.yaml"


async def turn(hass, socket, kind, value=0, index=1):
    await socket.queue.put({"t": "input", "deck": "A1", "kind": kind, "index": index, "value": value, "x": 0, "y": 0})
    await hass.async_block_till_done()


async def test_the_dial_dims_and_toggles(hass, entry, aioclient_mock, socket, tmp_path):
    hass.config.config_dir = str(tmp_path)
    target = Path(hass.config.path("blueprints/automation/deckconnector"))
    target.mkdir(parents=True)
    shutil.copy(BLUEPRINT, target)
    await setup(hass, entry, aioclient_mock, socket, PLUS)
    turn_on = async_mock_service(hass, "light", "turn_on")
    toggle = async_mock_service(hass, "light", "toggle")
    use = {
        "path": "deckconnector/dial_light.yaml",
        "input": {"controls": "event.stream_deck_controls", "dial": 2, "light": {"entity_id": "light.desk"}},
    }
    assert await async_setup_component(hass, "automation", {"automation": {"use_blueprint": use}})
    await hass.async_block_till_done()

    await turn(hass, socket, "encoder_turn", 3)
    await turn(hass, socket, "encoder_turn", -1)
    await turn(hass, socket, "encoder_turn", 1, index=0)
    await turn(hass, socket, "encoder_down")
    await turn(hass, socket, "encoder_up")
    assert [c.data["brightness_step_pct"] for c in turn_on] == [15, -5]
    assert len(toggle) == 1 and toggle[0].data["entity_id"] == ["light.desk"]
