#!/usr/bin/env bash
# Start the bot in a Docker container that runs in the background and restarts with the daemon.
#
# The image is always pulled from Docker Hub, so this runs the latest published release. The
# container shares the host network, so the bot reaches NapCat's OneBot HTTP server on the host
# (config.json's base_url expects 127.0.0.1:3000), and NapCat posts events to the bot on
# 127.0.0.1:8080. The startup configuration is mounted read-only from the repository (or
# ASSISTANTBOT_CONFIG), and the SQLite database is kept in a named volume, so both survive a new
# container.
#
# The four runtime credentials are taken from the exported environment; a native bot using port
# 8080 must be stopped first. `.env.example` is the template for other users and is not read here.
#
# Run this once. Afterwards use docker start, stop, and logs with the container name; running the
# script again fails while that container exists.

set -euo pipefail

readonly container_name=assistantbot
readonly image=yizhinailong/assistantbot:latest
readonly volume_name=assistantbot-data

repo_dir=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)
readonly repo_dir

# Resolve a relative override against the repository, as Compose does from its project directory.
config_file="${ASSISTANTBOT_CONFIG:-config.json}"
if [[ "$config_file" != /* ]]; then
    config_file="$repo_dir/$config_file"
fi
readonly config_file

# The app's plugins throw at startup when a named variable is missing, so report all four here.
for variable in ONEBOT_ACCESS_TOKEN ONEBOT_SECRET DEEPSEEK_API_KEY ASSISTANTBOT_ADMIN_TOKEN; do
    if [[ -z "${!variable:-}" ]]; then
        printf '%s is unset or empty; export it before running this script.\n' \
            "$variable" >&2
        exit 1
    fi
done

if [[ ! -f "$config_file" ]]; then
    printf 'Configuration file %s not found; set ASSISTANTBOT_CONFIG.\n' "$config_file" >&2
    exit 1
fi

docker pull "$image"

docker_args=(
    --detach
    --name "$container_name"
    --restart unless-stopped
    --network host
    --init
    --read-only
    --tmpfs /tmp:rw,nosuid,nodev,size=64m
    --cap-drop ALL
    --security-opt no-new-privileges:true
    --log-driver json-file
    --log-opt max-size=10m
    --log-opt max-file=3
    --env ONEBOT_ACCESS_TOKEN
    --env ONEBOT_SECRET
    --env DEEPSEEK_API_KEY
    --env ASSISTANTBOT_ADMIN_TOKEN
    --volume "$config_file:/app/config.json:ro"
    --volume "$volume_name:/app/data"
)

docker run "${docker_args[@]}" "$image"
