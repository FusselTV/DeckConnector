#!/bin/sh
# Runs a command in its own network namespace, so test suites with fixed ports can run side by side. The dummy LAN
# address is for tests that look up "this machine's address" (dbtools.server.local_ip).
set -e
exec unshare -rn sh -ec '
  ip link set lo up
  ip link add lan type dummy
  ip addr add 10.99.0.1/24 dev lan
  ip link set lan up
  ip route add default dev lan
  exec "$@"' sh "$@"
