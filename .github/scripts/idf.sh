#!/bin/sh
# Runs bash commands in the ESP-IDF image $IDF_IMAGE plus idf.Dockerfile. As the runner's user: files a root
# container leaves in the workspace break the next checkout. Under the workspace's own path: the simulators open
# their flash files at the path they were built in.
set -e
cc=${CCACHE_DIR:-$RUNNER_TEMP/ccache}
mkdir -p "$cc"
here=$(dirname "$0")
img=deckconnector-idf:$( (echo "$IDF_IMAGE"; cat "$here/idf.Dockerfile") | sha256sum | cut -c1-12)
if ! docker image inspect "$img" > /dev/null 2>&1; then
  docker image inspect "$IDF_IMAGE" > /dev/null 2>&1 || docker pull -q "$IDF_IMAGE"
  docker build -q --pull=false --build-arg IDF_IMAGE="$IDF_IMAGE" -t "$img" - < "$here/idf.Dockerfile"
fi
# ccache checks -specs files like the compiler; IDF rewrites picolibc.specs at every configure, so mtime never hits
exec docker run --rm --user "$(id -u):$(id -g)" -e HOME=/tmp -v "$GITHUB_WORKSPACE:$GITHUB_WORKSPACE" \
  -w "$GITHUB_WORKSPACE" -v "$cc:/ccache" -e CCACHE_DIR=/ccache -e CCACHE_COMPILERCHECK=content \
  -e CCACHE_BASEDIR="$GITHUB_WORKSPACE" "$img" bash -ec 'git config --global --add safe.directory "$PWD"
'"$1"
