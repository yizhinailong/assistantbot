# Docker

English · [简体中文](../zh-CN/docker.md)

[Documentation index](README.md) · [Project home](../../README.md)

## Start with Compose

Use Docker Engine on Linux with BuildKit and Docker Compose 2.24 or newer. Compose uses host networking, matching `scripts/docker_run_napcat.sh` and the loopback addresses in `config.json`. Run NapCat separately and configure its OneBot HTTP server and signed HTTP client as described in the [bot guide](bot.md).

From the repository root:

```sh
cp .env.example .env
```

Edit `.env` and fill all four values:

| Variable | Value |
| --- | --- |
| `ONEBOT_ACCESS_TOKEN` | NapCat's OneBot HTTP server token |
| `ONEBOT_SECRET` | NapCat's HTTP client token used to sign events |
| `DEEPSEEK_API_KEY` | Model API key named by the default configuration |
| `ASSISTANTBOT_ADMIN_TOKEN` | Your own administrator login token |

Single-quote values containing literal `$` characters. Compose reads `.env`; existing shell variables take precedence. Variables named by optional model roles and MCP services can also go in `.env`, which is forwarded to the container. `.env` is ignored by Git and excluded from the image build context. Compose rejects missing or empty values for the four required variables before creating a container.

```sh
docker compose up --detach --build
docker compose ps
docker compose logs --follow assistantbot
```

Open `http://127.0.0.1:8080` and sign in with `ASSISTANTBOT_ADMIN_TOKEN`. With the default configuration, NapCat's API is at `http://127.0.0.1:3000` and its event callback is `http://127.0.0.1:8080/onebot/event`. A native bot process using port 8080 must be stopped before starting this container.

The first build downloads the toolchain and compiles dependencies. BuildKit caches pnpm packages, vcpkg downloads/binaries, CPM sources, and C++ compilation. Builds default to two parallel jobs; set `BUILD_JOBS` in `.env` to change the Compose build limit. To build the image without any runtime secrets:

```sh
docker build --tag assistantbot:local .
# Optional: increase parallel compilation on a machine with sufficient memory.
docker build --build-arg BUILD_JOBS=4 --tag assistantbot:local .
```

The equivalent helpers are `just docker-build`, `just docker-up`, `just docker-down`, and `just docker-logs`. `docker-build` builds the image directly; `docker-up` builds and starts through Compose.

If dependency downloads require your host's HTTP proxy, Docker accepts the predefined `HTTP_PROXY` and `HTTPS_PROXY` build arguments. The proxy must be reachable from the builder. For a proxy listening on Linux host loopback:

```sh
docker build --network=host --build-arg HTTP_PROXY --build-arg HTTPS_PROXY \
  --tag assistantbot:local .
docker compose up --detach --no-build
```

This reads the proxy variables already exported in your shell. Model keys and bot tokens remain runtime variables and are not build arguments.

## Use a Docker Hub image

Published `linux/amd64` images are available at [yizhinailong/assistantbot](https://hub.docker.com/r/yizhinailong/assistantbot). Tags such as `0.1.0` use `project()`'s `VERSION` in `CMakeLists.txt`; `latest` is updated on each successful publication.

After filling the four runtime credentials, set the image in `.env`:

```dotenv
ASSISTANTBOT_IMAGE=yizhinailong/assistantbot:0.1.0
```

Then pull and start without building locally:

```sh
docker compose pull assistantbot
docker compose up --detach --no-build
```

For updates, select the next published version and repeat those commands. You can also select `latest`. Omitting `ASSISTANTBOT_IMAGE` keeps the local-build default `assistantbot:local`. Both paths use the same config and persistent data volume.

Every change to `project(VERSION ...)` pushed to `master` creates a version tag. The tag's Linux CI publishes the image after native tests and frontend checks pass. Credential setup and retries are described in the [CI guide](ci.md).

## Configuration and persistence

Compose mounts `config.json` read-only at `/app/config.json`. The image also contains the tracked configuration for runs outside Compose. To keep local changes in a separate file:

```sh
cp config.json config.local.json
```

Set `ASSISTANTBOT_CONFIG=./config.local.json` in `.env` and edit that file. Git ignores it. Keep the existing environment-variable names unless you also adjust Compose's required variables. Changes made through the admin pages remain in SQLite and do not rewrite either config file.

| Container path | Purpose |
| --- | --- |
| `/app/config.json` | Read-only startup configuration |
| `/app/data` | SQLite database in the Compose `assistantbot-data` volume |
| `/app/frontend/dist` | Built admin pages included in the image |
| `/tmp` | Temporary files, backed by a 64 MiB tmpfs in Compose |

Keep `data_dir` set to relative `data` or absolute `/app/data` unless you also change the writable volume mount. Upload paths must be under `/tmp` or another writable mount.

The process runs as UID/GID `10001:10001` with a read-only root filesystem. Docker initializes a new named volume with the data directory's ownership. A custom bind mount for data must be writable by that UID. The default volume is separate from a native installation's `./data`; moving an existing database requires a consistent copy taken while its writers are stopped.

Recreate the container after changing `.env` or startup configuration:

```sh
docker compose up --detach --force-recreate
```

For source updates, run `docker compose up --detach --build`. Runtime settings edited in the admin pages retain their normal hot-update behavior. Model context, login sessions, and in-memory logs reset when the process restarts; settings, messages, usage, profiles, memory, assets, and tasks remain in the data volume.

Stop the service while retaining its data:

```sh
docker compose down
```

`docker compose down --volumes` removes the persistent volume as well. To copy the data directory for a backup while the container still exists:

```sh
docker compose stop assistantbot
docker compose cp assistantbot:/app/data ./backup
docker compose start assistantbot
```

## Networking and health

Host networking makes the container share the Linux host's loopback interface. There is no `ports` mapping; the address and port in `config.json` control the listener. The default remains `127.0.0.1:8080`. Use a host reverse proxy with TLS for remote admin access.

The image healthcheck requests `GET /api/admin/health` every 30 seconds. It checks the bot's HTTP service and does not test NapCat availability, model credentials, or model quota. If you change the listener's port, update the healthcheck URL in a Compose override too. Compose rotates container logs at 10 MiB and keeps three files.

The supplied Compose file targets Linux host networking. For a bridge deployment, supply a configuration that listens on `0.0.0.0`, publish the admin port on the desired host interface, and use a NapCat API address reachable from that container. NapCat's callback must also reach the published bot port; container loopback addresses refer to the container itself.

## Image build

The frontend stage uses Node.js 24 and the project's pnpm version to build the admin pages. The native stage uses Ubuntu 24.04, Clang 21, libstdc++ 14, checksum-verified CMake 4.2.3, and the same vcpkg commit as Linux CI. Drogon with SQLite and the C++ Lua runtime are built through vcpkg; CPM resolves the library release tags in `CMakeLists.txt`.

The runtime stage contains the stripped release executable, frontend files, default config, CA certificates, and runtime libraries. `SSL_CERT_FILE` and `SSL_CERT_DIR` point the linked OpenSSL client to the runtime's system certificates. Build tools and secrets are not copied into it. The Dockerfile selects verified CMake archives for `amd64` and `arm64`; runtime validation is performed on `amd64`.

MCP services using stdio need their executable and any interpreter installed in the runtime image. Add these in a derived image and configure paths that exist inside the container; the frontend's Node.js build stage does not install Node.js in the runtime. HTTP MCP services only need a reachable URL.

Docker references: [multi-stage builds](https://docs.docker.com/build/building/multi-stage/), [build cache](https://docs.docker.com/build/cache/optimize/), and [Compose services](https://docs.docker.com/reference/compose-file/services/).
