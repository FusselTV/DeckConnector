#!/bin/sh
case "$2" in */ldgen.py) exec "$1" "$(dirname "$0")/ldgen_cache.py" "$@" ;; esac
exec "$@"
