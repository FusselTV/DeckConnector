#!/bin/sh
# Runs bash commands in the ESP-IDF image $IDF_IMAGE plus idf.Dockerfile, as the runner's user: files a root
# container leaves in the workspace break the next checkout. The workspace is mounted at $IDF_MOUNT: /w for the
# firmware, the same on every runner so they share ccache hits; the simulators keep the workspace's own path,
# because they open their flash files at the path they were built in.
set -e
cc=${CCACHE_DIR:-$RUNNER_TEMP/ccache}
w=${IDF_MOUNT:-$GITHUB_WORKSPACE}
mkdir -p "$cc"
here=$(dirname "$0")
img=deckconnector-idf:$( (echo "$IDF_IMAGE"; cat "$here/idf.Dockerfile") | sha256sum | cut -c1-12)
if ! docker image inspect "$img" > /dev/null 2>&1; then
  docker image inspect "$IDF_IMAGE" > /dev/null 2>&1 || docker pull -q "$IDF_IMAGE"
  docker build -q --pull=false --build-arg IDF_IMAGE="$IDF_IMAGE" -t "$img" - < "$here/idf.Dockerfile"
fi
# ccache checks -specs files like the compiler; IDF rewrites picolibc.specs at every configure, so mtime never hits
exec docker run --rm --user "$(id -u):$(id -g)" -e HOME=/tmp -v "$GITHUB_WORKSPACE:$w" -w "$w" -v "$cc:/ccache" \
  -e CCACHE_DIR=/ccache -e CCACHE_COMPILERCHECK=content "$img" bash -ec 'git config --global --add safe.directory "$PWD"
'"$1"
