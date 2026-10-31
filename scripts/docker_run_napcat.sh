#!/usr/bin/env bash
# Start NapCat in a Docker container that runs in the background and restarts with the daemon.
#
# The container shares the host network, so the bot reaches NapCat's OneBot HTTP server on the host
# (config.json's base_url expects 127.0.0.1:3000), and NapCat posts events to the bot on
# 127.0.0.1:8080. NapCat's configuration and the QQ login data are kept under data_dir on the host,
# so they survive a new container, and NapCat runs as the current user so that it can write them.
#
# Run this once. Afterwards use docker start, stop, and logs with the container name; running the
# script again fails while that container exists.

set -euo pipefail

readonly container_name=napcat
readonly image=mlikiowa/napcat-docker:latest
readonly data_dir="$HOME/.config/napcat"

# Docker creates a missing bind-mount source owned by root, which NapCat could not write to.
mkdir -p "$data_dir/config" "$data_dir/qq"

docker run --detach \
    --name "$container_name" \
    --restart unless-stopped \
    --network host \
    --env NAPCAT_UID="$(id -u)" \
    --env NAPCAT_GID="$(id -g)" \
    --volume "$data_dir/config:/app/napcat/config" \
    --volume "$data_dir/qq:/app/.config/QQ" \
    "$image"
