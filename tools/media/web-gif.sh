#!/bin/sh
# Records .github/assets/web-ui.gif from the demo build. Needs npm ci in web/ and ffmpeg.
set -eu
cd "$(dirname "$0")/../.."
WORK=${MEDIA_WORK:-$HOME/.cache/deckbridge/media}
PORT=${DEMO_PORT:-5611}
mkdir -p "$WORK"
(cd web && npm run -s build:demo >/dev/null)
(cd web && exec node_modules/.bin/vite preview --mode demo --host 127.0.0.1 --port "$PORT" --strictPort) >"$WORK/preview.log" 2>&1 &
PREVIEW=$!
trap 'kill $PREVIEW 2>/dev/null' EXIT
until curl -sf "http://127.0.0.1:$PORT/" >/dev/null; do kill -0 $PREVIEW; sleep 0.2; done
node tools/media/web-gif.mjs "http://127.0.0.1:$PORT/" "$WORK/web"
tools/media/gif.sh "$WORK/web/list.txt" 1280:756:0:0 860 .github/assets/web-ui.gif
