#!/bin/sh
# Records the web UI GIF and the Home Assistant pictures and GIFs for the README and the docs. Needs Docker, uv, ffmpeg and npm ci in web/.
set -eu
cd "$(dirname "$0")/../.."
WORK=${MEDIA_WORK:-$HOME/.cache/deckbridge/media}
PORT=${DEMO_PORT:-5611}
mkdir -p "$WORK"
(cd web && npm run -s build:demo >/dev/null)
(cd web && exec node_modules/.bin/vite preview --mode demo --host 127.0.0.1 --port "$PORT" --strictPort) >"$WORK/preview.log" 2>&1 &
PREVIEW=$!
trap 'kill $PREVIEW 2>/dev/null; docker rm -f dc-media-ha >/dev/null 2>&1' EXIT
until curl -sf "http://127.0.0.1:$PORT/" >/dev/null; do kill -0 $PREVIEW; sleep 0.2; done
node tools/media/grab.mjs "http://127.0.0.1:$PORT/" "$WORK/board"

uv run -q --project tools/ha python tools/media/ha-up.py "$WORK"
node tools/media/ha-shots.mjs "$WORK/ha"
uv run -q --with pillow python tools/media/webp.py "$WORK/ha"
for lang in en de; do
  out=$(node tools/media/ha-gif.mjs "$lang" "$WORK/gif-$lang")
  crop=$(printf '%s\n' "$out" | sed -n 's/^crop //p')
  [ -n "$crop" ]
  tools/media/gif.sh "$WORK/gif-$lang/list.txt" "$crop" 860 "site/public/docs/ha-live-$lang.gif"
done
node tools/media/web-gif.mjs "http://127.0.0.1:$PORT/" "$WORK/web"
tools/media/gif.sh "$WORK/web/list.txt" 1280:756:0:0 860 .github/assets/web-ui.gif
tools/media/gif.sh "$WORK/web/ha/list.txt" iw:ih:0:0 iw .github/assets/ha-connector.gif
