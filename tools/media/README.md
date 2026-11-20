# Media

Re-records the pictures in the README and the docs. It writes the files in place. Run it from anywhere, then
look at the result and commit it.

| Command | Writes |
|---|---|
| `tools/media/ha.sh` | `.github/assets/web-ui.gif`, `ha-connector.gif` and `ha-*.webp`, `site/public/docs/ha-*.webp` and `ha-live-*.gif` |

It needs `npm ci` in `web/`, ffmpeg, Docker and uv. It starts Home Assistant with the integration
from this checkout in the container `dc-media-ha` (ports 18140 and 18141) and removes it at the end. Instead of a board,
`board.py` runs in that container and serves the demo's status and deck pictures. Frames and PNGs go to
`~/.cache/deckbridge/media` (`MEDIA_WORK`).

It records `web-ui.gif` and `ha-connector.gif` on one timeline. Both have the same length and frame times, so the
README shows the same volume and brightness in both. The demo sends its values to `board.py`, and Home Assistant gets them
over the same socket it uses with a board.

To change a GIF, edit the `SCENES` list at the top of `web-gif.mjs` or `ha-gif.mjs`.
`ha-shots.mjs` takes the pictures, and `webp.py` turns them into the files and writes contact sheets to the work folder.
