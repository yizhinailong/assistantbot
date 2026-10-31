# assistantbot

[English](README.md) · 简体中文

基于 [drogon](https://github.com/drogonframework/drogon) 的 QQ 私聊机器人。它通过 [drogon-onebot](https://github.com/yizhinailong/drogon-onebot) 接收 [NapCat](https://github.com/NapNeko/NapCatQQ) 等 OneBot v11 实现转来的消息，再通过 [drogon-openai](https://github.com/yizhinailong/drogon-openai) 调用 DeepSeek 等兼容 OpenAI 格式的模型来回复。

目前只回复私聊里的文字消息，默认回复该 QQ 账号的所有好友。管理后台可以限制为只回复允许名单内的好友，并用黑名单拦截指定用户。每个用户的最近几轮模型上下文保存在内存里。管理后台提供实时聊天记录、上下文重置、回复设置、UTC 用量图表和日志过滤；设置、消息和用量保存在 SQLite 中。

[快速开始](#快速开始) · [文档索引](docs/zh-CN/README.md) · [bot 使用说明](docs/zh-CN/bot.md) · [管理后台](docs/zh-CN/admin.md) · [版本标签与 Linux CI](docs/zh-CN/ci.md) · [Doxygen 使用说明](docs/zh-CN/doxygen.md)

## 环境与依赖

| 依赖 | 版本 | 说明 |
| --- | --- | --- |
| CMake | ≥ 4.0 | 构建使用 `CMakePresets.json` 中的 `<os>-<profile>` 预设 |
| Ninja、ccache | 任意较新版本 | 预设固定使用 Ninja 生成器和 ccache 编译器启动器 |
| 编译器 | Linux 上为 `clang++`，Windows 上为 `clang-cl` | 项目使用 C++26 |
| [vcpkg](https://github.com/microsoft/vcpkg) | classic 模式 | 提供 drogon，需设置 `VCPKG_ROOT` |
| drogon | 1.9.13 | 由 vcpkg 安装，需要 `sqlite3` 特性，`just init` 执行 `vcpkg install 'drogon[sqlite3]'` |
| drogon-onebot、drogon-openai | `v0.4.0`、`v0.2.0` | 在 `CMakeLists.txt` 中固定的发布标签。首次配置时由 CPM 从 GitHub 克隆（需要网络），设置了 `CPM_SOURCE_CACHE` 时克隆到该目录，否则克隆到构建目录 |
| Node.js、pnpm | `^22.18.0` 或 `>=24.12.0`、12.15.0 | 只有 `frontend/` 中的管理后台需要，`just frontend-init` 安装它的依赖。见 [管理后台](docs/zh-CN/admin.md) |

[Linux CI](.github/workflows/linux-ci.yml) 在 Ubuntu 24.04 上运行 debug 和 release 两套配置：

- 工具：CMake 4.2.3、LLVM 21，vcpkg 固定在提供 drogon 1.9.13 的提交；
- 内容：检查格式、构建并运行全部测试；
- 另有一个任务用 Node.js 24 对管理后台做类型检查、lint 和构建。

详见 [版本标签与 Linux CI](docs/zh-CN/ci.md)。

<a id="快速开始"></a>

## 快速开始

在仓库根目录安装依赖、构建并运行测试：

```sh
just init
just build
just test
```

测试使用 drogon 自带的测试框架，全部离线运行：端到端测试由同一个 app 同时充当 NapCat 的接口和模型接口。不使用 just 时，`cmake --workflow --preset linux-debug` 依次完成配置、构建和测试。

运行 bot 前，先设置 `config.json` 指定的四个环境变量：

```sh
export ONEBOT_ACCESS_TOKEN=<NapCat HTTP 服务器的 token>
export ONEBOT_SECRET=<NapCat HTTP 客户端的 token>
export DEEPSEEK_API_KEY=<你的 DeepSeek API key>
export ASSISTANTBOT_ADMIN_TOKEN=<你的管理员令牌>
just run
```

bot 监听 `127.0.0.1:8080`，在 `/onebot/event` 接收事件，按访问策略回复好友的私聊消息。包含访问策略、回复设置、聊天记录和用量的 SQLite 数据库保存在 `data/` 中，Git 会忽略这个目录。`config.json` 已提交到仓库，其中没有任何私密信息：密钥只从环境变量读取，`config.json` 里只写变量名。每个配置项的含义，以及 NapCat 如何连接 bot，见 [bot 使用说明](docs/zh-CN/bot.md)。

## 不用 NapCat 试用

`scripts/chat.py` 可以代替 NapCat：它在 `127.0.0.1:3000`（`config.json` 里 `base_url` 指向的地址）提供 OneBot HTTP 接口，并把你在终端输入的每一行，作为好友发来的私聊消息签名后发给 bot。按上文启动 bot 后，在另一个终端里设置同样的 `ONEBOT_ACCESS_TOKEN` 和 `ONEBOT_SECRET`，然后运行：

```sh
python3 scripts/chat.py
```

bot 的回复来自真实的模型。脚本只用到 Python 标准库，`--help` 会列出它的选项。

## 开发与文档

使用文档分为英文版（`docs/en/`）和简体中文版（`docs/zh-CN/`），两边的文件一一对应，API 标识符、命令和协议值保留原文；新增文档请在两边各写一份，并加入各自的 [文档索引](docs/zh-CN/README.md)。本文是 [README.md](README.md) 的简体中文版，修改其中一份时请同步更新另一份。开发约定见 [AGENTS.md](AGENTS.md)：

- 代码风格；
- 用 `clang-format --dry-run -Werror` 检查格式；
- 每次改动行为时同步更新文档。

安装 Doxygen 后，执行 `just docs` 生成 API 文档，首页为 `build/doxygen/html/index.html`。推送到 `master` 时，Documentation 工作流会自动生成文档并发布到 GitHub Pages。首次启用所需的设置见 [Doxygen 使用说明](docs/zh-CN/doxygen.md)。

## 许可证

采用 [MIT 许可证](LICENSE)。
