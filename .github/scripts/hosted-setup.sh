#!/bin/sh
# The ESP-IDF image does not fit on a GitHub-hosted runner's root disk, /mnt has room. Ubuntu's AppArmor blocks the
# unprivileged namespaces netns.sh and simnet make.
set -e
sudo systemctl stop docker docker.socket
{ cat /etc/docker/daemon.json 2> /dev/null || echo '{}'; } | jq '. + {"data-root": "/mnt/docker"}' > "$RUNNER_TEMP/daemon.json"
sudo mv "$RUNNER_TEMP/daemon.json" /etc/docker/daemon.json
sudo systemctl start docker
sudo sysctl -qw kernel.apparmor_restrict_unprivileged_userns=0
df -h / /mnt
