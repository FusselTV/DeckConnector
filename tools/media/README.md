# Media

Re-records the pictures in the README and the docs. Each command writes its files in place. Run it from anywhere, then
look at the result and commit it.

| Command | Writes |
|---|---|
| `tools/media/web-gif.sh` | `.github/assets/web-ui.gif` from the demo build |
| `tools/media/ha.sh` | `.github/assets/ha-*.webp` and `sync.gif`, `site/public/docs/ha-*.webp` and `ha-live-*.gif` |

Both need `npm ci` in `web/` and ffmpeg. `ha.sh` also needs Docker and uv. It starts Home Assistant with the integration
from this checkout in the container `dc-media-ha` (ports 18140 and 18141) and removes it at the end. Instead of a board,
`board.py` runs in that container and serves the demo's status and deck pictures. Frames and PNGs go to
`~/.cache/deckbridge/media` (`MEDIA_WORK`).

For `sync.gif` the demo sends its volume and brightness to `board.py`. Home Assistant gets them over the same socket
it uses with a board.

To change a GIF, edit the `SCENES` list at the top of `web-gif.mjs`, `ha-gif.mjs` or `sync-gif.mjs`.
`ha-shots.mjs` takes the pictures, and `webp.py` turns them into the files and writes contact sheets to the work folder.
