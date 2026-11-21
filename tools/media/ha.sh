#!/bin/sh
# Records the README GIF and the Home Assistant pictures and GIFs for the docs. Needs Docker, uv, ffmpeg and npm ci in web/.
set -eu
cd "$(dirname "$0")/../.."
# a busy host stretches the recording and the web UI and Home Assistant drift apart
load=$(cut -d' ' -f1 /proc/loadavg)
if [ "${load%.*}" -ge 4 ]; then echo "load $load: wait for a quiet host" >&2; exit 1; fi
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
out=$(node tools/media/web-gif.mjs "http://127.0.0.1:$PORT/" "$WORK/web")
printf '%s\n' "$out"
crop=$(printf '%s\n' "$out" | sed -n 's/^crop //p')
[ -n "$crop" ]
uv run -q --with pillow python tools/media/readme.py "$WORK/web" "$crop" "$WORK/readme.gif"
mkdir -p .github/assets
docker run --rm -i alpine:3.20 sh -c 'apk add -q gifsicle=1.95-r0 >/dev/null && gifsicle -O3 --lossy=20' <"$WORK/readme.gif" >"$WORK/readme-small.gif"
mv "$WORK/readme-small.gif" .github/assets/readme.gif
echo ".github/assets/readme.gif: $(stat -c %s .github/assets/readme.gif) bytes"
