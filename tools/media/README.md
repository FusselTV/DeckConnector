# Media

Re-records the pictures in the README and the docs. Each command writes its files in place. Run it from anywhere, then
look at the result and commit it.

| Command | Writes |
|---|---|
| `tools/media/web-gif.sh` | `.github/assets/web-ui.gif` from the demo build |
| `tools/media/ha.sh` | `.github/assets/ha-*.webp`, `site/public/docs/ha-*.webp` and `ha-live-*.gif` |

Both need `npm ci` in `web/` and ffmpeg. `ha.sh` also needs Docker and uv. It starts Home Assistant with the integration
from this checkout in the container `dc-media-ha` (ports 18140 and 18141) and removes it at the end. Instead of a board,
`board.py` runs in that container and serves the demo's status and deck pictures. Frames and PNGs go to
`~/.cache/deckbridge/media` (`MEDIA_WORK`).

To change a GIF, edit the `SCENES` list at the top of `web-gif.mjs` or `ha-gif.mjs`. `ha-shots.mjs` takes the
pictures, and `webp.py` turns them into the files and writes contact sheets to the work folder.
