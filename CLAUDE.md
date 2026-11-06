# Bench rules

The bench bridge (192.168.1.214) is also the owner's production deck. Companion and the Stream Deck app behind it
switch real lights, scenes, a vacuum and more through Home Assistant, MQTT and HTTP.

- Simulated presses (`POST /api/input`, `{"t": "input"}` on `/api/ws`, encoder turns, touches) must never reach a real
  host. Firmware that reports `simulated_input: "local"` in `/api/status` keeps them away from Companion, the Stream
  Deck app and Satellite unless the request carries `"real"`.
- Never send `"real": true` from a test, tool or script: it is for a person at the web UI. Tools send
  `"real": "<ip>"` with the address from `dbtools.safety.real_input_target`, which allows only a host on this
  machine, and the firmware forwards each press only while that host draws. `tests/test_input_safety.py` and
  `conftest.api` enforce this.
- Never press anything on firmware without that gate: `dbtools.safety.require_input_gate` before the first press.
  `tests/conftest.py` ends the whole run when the bridge lacks it, also after a slot switch.
- Ad-hoc scripts follow the same rules as tests. When in doubt, point them at a dock host or test Companion on this
  machine (`tools/companion-check`, a local Companion container), never at the production Companion (192.168.1.15).
- After any test, the bridge goes back to its production settings (`controller: companion`, `host: satellite`,
  `satellite: wss://companion.example.com/satellite`), and core draws again.
