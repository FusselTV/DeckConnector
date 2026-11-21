"""Starts Home Assistant in Docker with the integration from this checkout and board.py inside the same container,
then adds the board through the config flow. uv run --project tools/ha python tools/media/ha-up.py <work dir>"""

import asyncio
import contextlib
import shutil
import subprocess
import sys
from pathlib import Path

import aiohttp

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "tools/ha/e2e"))
import test_docker as e  # noqa: E402

IMAGE = "ghcr.io/home-assistant/home-assistant:2026.11.2"
NAME = "dc-media-ha"
HOST = "studio-deck.local"  # in the flow picture
WORK = Path(sys.argv[1])
e.HA = "http://127.0.0.1:18140"
e.CLIENT = f"{e.HA}/"


def docker(*args: str) -> None:
    subprocess.run(["docker", *args], check=True, stdout=subprocess.DEVNULL)


def start() -> None:
    subprocess.run(["docker", "rm", "-f", NAME], capture_output=True)
    cfg = WORK / "config"
    if cfg.exists():  # files written as root by the container
        docker("run", "--rm", "-v", f"{WORK}:/w", "--entrypoint", "rm", IMAGE, "-rf", "/w/config")
    (cfg / "custom_components").mkdir(parents=True)
    shutil.copytree(
        ROOT / "custom_components/deckconnector",
        cfg / "custom_components/deckconnector",
        ignore=shutil.ignore_patterns("__pycache__"),
    )
    (cfg / "configuration.yaml").write_text("default_config:\n")
    docker(
        "run", "-d", "--name", NAME, "-p", "127.0.0.1:18140:8123", "-p", "127.0.0.1:18141:80",
        "--add-host", f"{HOST}:127.0.0.1", "-v", f"{cfg}:/config",
        "-v", f"{ROOT / 'tools/media'}:/dc-media:ro", "-v", f"{WORK / 'board'}:/board:ro", IMAGE,
    )  # fmt: skip
    docker("exec", "-d", NAME, "sh", "-c", "python3 /dc-media/board.py /board 80 >/tmp/board.log 2>&1")


async def add_board() -> None:
    async with aiohttp.ClientSession() as s:
        await e.wait_for_ha(s)
        for _ in range(20):
            with contextlib.suppress(aiohttp.ClientError):
                async with s.post("http://127.0.0.1:18141/api/tokens") as r:
                    if r.status == 200:
                        break
            await asyncio.sleep(0.5)
        else:
            raise RuntimeError("board.py does not answer")
        auth = {"Authorization": f"Bearer {await e.onboard(s)}"}
        r = await e.flow(s, auth, None, {"handler": "deckconnector"})
        r = await e.flow(s, auth, r["flow_id"], {"host": HOST})
        r = await e.flow(s, auth, r["flow_id"], {"next_step_id": "password"})
        r = await e.flow(s, auth, r["flow_id"], {"password": "demo"})
        assert r["type"] == "create_entry", r


start()
try:
    asyncio.run(add_board())
except RuntimeError:
    subprocess.run(["docker", "exec", NAME, "cat", "/tmp/board.log"])
    raise
