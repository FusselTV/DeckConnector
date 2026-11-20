#!/bin/sh
# Runs bash commands in the ESP-IDF image $IDF_IMAGE. As the runner's user: files a root container leaves in the
# workspace break the next checkout. Under the workspace's own path: the simulators open their flash files at the
# path they were built in.
set -e
cc=${CCACHE_DIR:-$RUNNER_TEMP/ccache}
mkdir -p "$cc"
docker image inspect "$IDF_IMAGE" > /dev/null 2>&1 || docker pull -q "$IDF_IMAGE"
# CCACHE_COMPILERCHECK=content: IDF writes build/specs/picolibc.specs at every configure, and ccache's default
# check by mtime missed every compile of a fresh checkout
exec docker run --rm --user "$(id -u):$(id -g)" -e HOME=/tmp -v "$GITHUB_WORKSPACE:$GITHUB_WORKSPACE" \
  -w "$GITHUB_WORKSPACE" -v "$cc:/ccache" -e CCACHE_DIR=/ccache -e CCACHE_MAXSIZE=5G -e CCACHE_COMPILERCHECK=content \
  -e CCACHE_BASEDIR="$GITHUB_WORKSPACE" "$IDF_IMAGE" bash -ec 'git config --global --add safe.directory "$PWD"
'"$1"
