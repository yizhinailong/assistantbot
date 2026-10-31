# assistantbot

English · [简体中文](README.zh-CN.md)

A QQ chat bot built on [drogon](https://github.com/drogonframework/drogon). It receives messages from a OneBot v11 implementation such as [NapCat](https://github.com/NapNeko/NapCatQQ) through [drogon-onebot](https://github.com/yizhinailong/drogon-onebot), and answers them with an OpenAI-compatible model such as DeepSeek through [drogon-openai](https://github.com/yizhinailong/drogon-openai).

The bot answers friends by default and supports opt-in group conversations. Admin pages manage access, live chat records, reply settings, independent model roles, memory, scheduled tasks, built-in and Lua tools, MCP services, favourite stickers, character images, usage, logs, and HTTP traces. SQLite persists configuration and records; short-term model context stays in memory. Optional model roles, memory, and tools start disabled.

[Quick start](#quick-start) · [Docker](docs/en/docker.md) · [Documentation index](docs/en/README.md) · [Bot guide](docs/en/bot.md) · [Admin pages](docs/en/admin.md) · [Version tags and Linux CI](docs/en/ci.md) · [Doxygen guide](docs/en/doxygen.md)

## Requirements

| Dependency | Version | Notes |
| --- | --- | --- |
| CMake | ≥ 4.0 | The build uses the `<os>-<profile>` presets in `CMakePresets.json` |
| Ninja, ccache | Any recent version | The presets always use the Ninja generator and the ccache compiler launcher |
| Compiler | `clang++` on Linux, `clang-cl` on Windows | The project uses C++26 |
| [vcpkg](https://github.com/microsoft/vcpkg) | Classic mode | Provides drogon and Lua; `VCPKG_ROOT` must be set |
| drogon | 1.9.13 | Installed by vcpkg with its `sqlite3` feature; `just init` runs `vcpkg install 'drogon[sqlite3]' 'lua[cpp]'` |
| drogon-onebot, drogon-openai | `v0.5.0`, `v0.3.0` | Release tags pinned in `CMakeLists.txt`. CPM clones them from GitHub on the first configure, which needs network access, into `$CPM_SOURCE_CACHE` when it is set or into the build tree otherwise |
| Lua | 5.5.1 with `cpp` | Installed by vcpkg as `lua[cpp]`; the C++ build preserves exception unwinding in bounded custom tools |
| Node.js, pnpm | `^22.18.0` or `>=24.12.0`, 12.15.0 | Only for the admin pages in `frontend/`; `just frontend-init` installs their dependencies. See the [admin guide](docs/en/admin.md) |

[Linux CI](.github/workflows/linux-ci.yml) runs a debug and a release configuration on Ubuntu 24.04:

- Tools: CMake 4.2.3 and LLVM 21, with vcpkg pinned to the commit that provides drogon 1.9.13.
- Steps: check the formatting, build, and run all tests.
- A separate job type-checks, lints, and builds the admin pages with Node.js 24.

See [Version tags and Linux CI](docs/en/ci.md).

<a id="quick-start"></a>

## Quick start

From the repository root, install the dependencies, build, and run the tests:

```sh
just init
just build
just test
```

The tests use drogon's own test framework and run offline: the end-to-end test serves a fake NapCat API and a fake model from the same app. Without just, `cmake --workflow --preset linux-debug` configures, builds, and tests in one go.

To run the bot, set the four environment variables that `config.json` names:

```sh
export ONEBOT_ACCESS_TOKEN=<token of NapCat's HTTP server>
export ONEBOT_SECRET=<token of NapCat's HTTP client>
export DEEPSEEK_API_KEY=<your DeepSeek API key>
export ASSISTANTBOT_ADMIN_TOKEN=<your administrator token>
just run
```

The bot listens on `127.0.0.1:8080`, receives events at `/onebot/event`, and answers private friend messages according to its access policy. It keeps its SQLite database, including access and reply settings, chat records, and usage, in `data/`, which Git ignores. `config.json` is tracked and holds nothing private: secrets are read only from environment variables, and it holds only their names. The [bot guide](docs/en/bot.md) describes every key, and how NapCat connects to the bot.

## Docker

On Linux with Docker Engine and Compose 2.24 or newer, build and run the bot and admin pages without installing the native toolchain on the host:

```sh
cp .env.example .env
# Fill the four required tokens/keys in .env, then start:
docker compose up --detach --build
```

Compose uses host networking, a persistent SQLite volume, and a read-only configuration. NapCat runs separately. Open `http://127.0.0.1:8080`; see the [Docker guide](docs/en/docker.md) for configuration, backups, and updates.

## Trying it without NapCat

`scripts/chat.py` stands in for NapCat. It serves the OneBot HTTP API on `127.0.0.1:3000`, where `base_url` in `config.json` points, and posts every line you type to the bot as a signed private message from a friend. With the bot running as above, in a second terminal with the same `ONEBOT_ACCESS_TOKEN` and `ONEBOT_SECRET`:

```sh
python3 scripts/chat.py
```

The bot's replies come from the real model. The script needs only the Python standard library; `--help` lists its options.

## Development and documentation

The documentation comes in English (`docs/en/`) and Simplified Chinese (`docs/zh-CN/`), with matching file names, and API identifiers, commands, and protocol values left as they are. Write a new page in both languages and add it to each [documentation index](docs/en/README.md). [README.zh-CN.md](README.zh-CN.md) is the Simplified Chinese version of this file; when you change one, update the other. The development conventions are in [AGENTS.md](AGENTS.md):

- the code style;
- checking the formatting with `clang-format --dry-run -Werror`;
- updating the documentation with every behavior change.

With Doxygen installed, `just docs` generates the API documentation. Its home page is `build/doxygen/html/index.html`. On pushes to `master`, the Documentation workflow generates the documentation and publishes it to GitHub Pages. The [Doxygen guide](docs/en/doxygen.md) describes the settings needed the first time.

## License

Released under the [MIT License](LICENSE).
