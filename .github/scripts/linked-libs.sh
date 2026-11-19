#!/bin/sh
# The libraries the firmware links, against the list THIRD_PARTY_LICENSES.txt was written for.
# Build-tree paths stay so that two archives with one name differ; toolchain paths drop to the name.
set -eu
here=$(dirname "$0")
map=firmware/build/deckconnector.map
test -s "$map" || { echo "::error::$map is missing"; exit 1; }
awk '/^Archive member included/ {on = 1} /^Memory Configuration/ {on = 0} on' "$map" |
  sed -n 's/^\([^ (][^(]*\.a\)(.*/\1/p' | sed 's|^/.*/||' | LC_ALL=C sort -u | diff -u "$here/linked-libs.txt" - || {
  echo "::error::the linked libraries changed. Update THIRD_PARTY_LICENSES.txt and the tables in site/content/docs/license.mdx and license.de.mdx, then $here/linked-libs.txt"
  exit 1
}
