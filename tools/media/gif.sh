#!/bin/sh
# gif.sh <concat list> <crop w:h:x:y> <width> <out.gif>
set -eu
# -t: the concat demuxer gives the last frame its own length once more
ffmpeg -loglevel error -y -f concat -safe 0 -i "$1" -t "$(awk '/^duration/ { s += $2 } END { print s }' "$1")" \
  -vf "fps=12,crop=$2,scale=$3:-1:flags=lanczos,split[a][b];[a]palettegen=max_colors=128:stats_mode=full[p];[b][p]paletteuse=dither=none:diff_mode=rectangle" \
  -loop 0 "$4"
echo "$4: $(stat -c %s "$4") bytes, $(ffprobe -v error -show_entries format=duration -of csv=p=0 "$4") s"
