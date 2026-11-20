#!/bin/sh
# Runs a command in its own network namespace, so the test suites of the four runners on one VM can all use their
# fixed ports. The dummy LAN address is for tests that look up "this machine's address" (dbtools.server.local_ip).
# A GitHub-hosted runner runs one job per VM and blocks unprivileged namespaces (AppArmor).
set -e
[ "$RUNNER_ENVIRONMENT" != github-hosted ] || exec "$@"
exec unshare -rn sh -ec '
  ip link set lo up
  ip link add lan type dummy
  ip addr add 10.99.0.1/24 dev lan
  ip link set lan up
  ip route add default dev lan
  exec "$@"' sh "$@"
