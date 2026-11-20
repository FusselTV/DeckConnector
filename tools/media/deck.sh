#!/bin/sh
# Draws the deck pictures for the docs (site/scripts/deck-shots.mjs). Needs gcc, a simulator build and npm ci in web/ and site/.
set -eu
cd "$(dirname "$0")/../.."
WORK=${MEDIA_WORK:-$HOME/.cache/deckbridge/media}
export DC_SIM_PORT="${DC_SIM_PORT:-18470}" DECK_SHOTS_TMP="$WORK"
PORT=${DECK_PORT:-5470}
mkdir -p "$WORK"
(cd web && node_modules/.bin/vite build --outDir "$WORK/web-dist" --emptyOutDir >/dev/null)
(cd web && exec node scripts/sim.mjs) >"$WORK/sim.log" 2>&1 &
SIM=$!
(cd web && DC_TARGET=http://127.0.0.1:$DC_SIM_PORT exec node_modules/.bin/vite preview --outDir "$WORK/web-dist" \
  --host 127.0.0.1 --port "$PORT" --strictPort) >"$WORK/deck-preview.log" 2>&1 &
PREVIEW=$!
trap 'kill $PREVIEW $SIM 2>/dev/null' EXIT
until curl -sf "http://127.0.0.1:$PORT/" >/dev/null && curl -s "http://127.0.0.1:$DC_SIM_PORT/" >/dev/null; do
  kill -0 $PREVIEW $SIM
  sleep 0.2
done
DC_SIM=http://127.0.0.1:$DC_SIM_PORT node site/scripts/deck-shots.mjs "http://127.0.0.1:$PORT"
