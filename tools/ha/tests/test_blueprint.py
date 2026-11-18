from pathlib import Path
import shutil

from homeassistant.setup import async_setup_component
from pytest_homeassistant_custom_component.common import async_mock_service
from test_setup import setup

from conftest import PLUS, status

BLUEPRINT = Path(__file__).resolve().parents[3] / "blueprints/automation/deckconnector/dial_light.yaml"


async def turn(hass, socket, kind, value=0, index=1):
    await socket.queue.put({"t": "input", "deck": "A1", "kind": kind, "index": index, "value": value, "x": 0, "y": 0})
    await hass.async_block_till_done()


async def test_the_dial_dims_and_toggles(hass, entry, aioclient_mock, socket, tmp_path):
    hass.config.config_dir = str(tmp_path)
    target = tmp_path / "blueprints/automation/deckconnector"
    await hass.async_add_executor_job(lambda: target.mkdir(parents=True) or shutil.copy(BLUEPRINT, target))
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

    await turn(hass, socket, "encoder_down")
    await socket.queue.put(None)
    await hass.async_block_till_done()
    await socket.opened.wait()
    await socket.queue.put({"t": "state", **status(PLUS)})
    await hass.async_block_till_done()
    assert len(toggle) == 2
