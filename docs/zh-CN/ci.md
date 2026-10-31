# 版本标签与 Linux CI

[English](../en/ci.md) · 简体中文

[文档索引](README.md) · [项目首页](../../README.zh-CN.md)

仓库有三个工作流，逻辑都直接写在 YAML 中，不依赖额外脚本：

| 工作流 | 作用 |
| --- | --- |
| [linux-ci.yml](../../.github/workflows/linux-ci.yml)（Linux CI） | 在分支推送、PR 和版本标签上构建并运行测试，并检查和构建管理后台 |
| [version-tag.yml](../../.github/workflows/version-tag.yml)（Create Version Tag） | `project()` 的 `VERSION` 变化时创建带注释的标签，并在该标签上启动 Linux CI |
| [docs.yml](../../.github/workflows/docs.yml)（Documentation） | 生成 Doxygen 文档并发布到 GitHub Pages，见 [Doxygen 使用说明](doxygen.md) |

应用不会被其他项目依赖，标签只用来记录版本，因此没有 Release 工作流，也不上传构建产物。目前只有 Linux CI。

## 版本标签

[version-tag.yml](../../.github/workflows/version-tag.yml) 在推送到 `master` 且修改了 `CMakeLists.txt` 时运行。

它读取推送前后两个提交中 `CMakeLists.txt` 第一个 `project()` 调用的 `VERSION`：

- 读取时先去掉 `#` 行注释，再取 `VERSION` 后面的字面量。
- 版本号格式为 `major[.minor[.patch[.tweak]]]`，不能是变量。
- 一次推送包含多个提交时，比较推送的两个端点。

版本变化时，在推送的最终提交上创建带注释的标签。例如版本从 0.1.0 变成 0.2.0：

- 标签为 `v0.2.0`，注释为 `assistantbot 0.2.0`；
- 创建者为 GitHub Actions 机器人。

不打标签的情况：

- 版本不变，只改了依赖、选项、格式或注释。
- 推送前的提交读不出版本，例如首次创建分支。这时它只作为比较基准。

已有标签不会被覆盖：

- 版本号已标记在其他提交上时，工作流失败，需要改用未占用的版本号。
- 重试同一次推送时复用已有标签，并再次启动该标签上的 Linux CI。

工作流不提交文件，也不会自动递增版本。

关于手动运行和并发：

- 支持在默认分支手动运行，此时比较所选提交及其第一父提交；版本没变仍不打标签。
- 多提交推送应重跑原工作流，以保留原始比较端点。
- 同一提交的并发尝试串行执行，不同版本的变更不会合并成一组。

创建标签后，工作流用内置令牌执行 `gh workflow run linux-ci.yml --ref <tag>`。原因是 `GITHUB_TOKEN` 发起的推送不会启动新的 push 工作流，但可以触发 `workflow_dispatch`，见 [GitHub 触发规则](https://docs.github.com/en/actions/how-tos/write-workflows/choose-when-workflows-run/trigger-a-workflow)。

标签任务请求 `contents: write` 和 `actions: write`，仓库规则必须允许它创建版本标签。版本工作流成功只表示标签存在、且已启动 Linux CI，不能据此判断测试通过；要看该标签上的 Linux CI 结果。

## Linux 构建和测试

[linux-ci.yml](../../.github/workflows/linux-ci.yml) 的触发方式：

- 向任意分支推送提交；
- 创建、更新或重新打开 PR；
- 从 Actions 页面手动运行；
- 由版本标签工作流在新标签上启动。

运行规则：

- 删除分支时跳过测试。
- 同一引用的重复运行会取消较早、尚未完成的运行。
- 每个任务检出触发事件对应的提交；PR 验证 GitHub 生成的合并提交。

`debug` 和 `release` 是两个独立的矩阵任务，一个失败不影响另一个完成验证。它们分别使用 `linux-debug` 和 `linux-release` 预设，在 `ubuntu-24.04` 上依次执行以下步骤：

1. 下载 CMake 4.2.3 官方 Linux 包，按固定的 SHA-256 校验。
2. 从 [apt.llvm.org](https://apt.llvm.org/) 安装 LLVM 21 的 `clang` 和 `clang-format`，再从 Ubuntu 软件源安装 Ninja 和 ccache。
   - 添加软件源前，先核对 LLVM 签名密钥的指纹。
   - 把 `/usr/lib/llvm-21/bin` 加入 `PATH`，让预设中的 `clang++` 指向 LLVM 21。
3. 仅 `debug` 任务：对所有被跟踪的 `.cpp` 和 `.hpp` 执行 `clang-format --dry-run -Werror`，规则来自根目录的 `.clang-format`。
4. 浅克隆 vcpkg 的固定提交 `67972e92`（提供 drogon 1.9.13 port-version 2，与本地验证的版本一致），执行 `bootstrap-vcpkg.sh` 和 `vcpkg install 'drogon[sqlite3]'`（带 SQLite 的 drogon，bot 的数据库需要它），并设置 `VCPKG_ROOT`。
   - vcpkg 的二进制缓存目录由 `actions/cache` 保存。
   - 缓存键包含运行器镜像、vcpkg 提交、构建 port 用的 GCC 版本、CMake 版本和安装的特性（`drogon-sqlite3`），其中任一项变化都会重建依赖。
   - 缓存在安装成功后立即保存，所以后续步骤失败也不影响缓存。
   - 依赖安装失败时，输出本次的 vcpkg 构建日志。
5. `cmake --preset`，然后 `cmake --build --preset`，最后 `ctest --preset`。
   - 配置时 CPM 从 GitHub 克隆 `CMakeLists.txt` 中固定的 drogon-openai 和 drogon-onebot 发布标签，所以在修改固定版本之前，CI 构建的库版本不变。
   - 配置时把 `CPM_SOURCE_CACHE` 设为一个由 `actions/cache` 恢复和保存的目录，缓存键覆盖 `CMakeLists.txt` 和 `cmake/CPM.cmake`。其中保存 CPM 本身和固定标签的克隆，所以缓存命中时不再克隆两个库；修改固定版本会改变缓存键。
   - test 预设在失败时输出测试日志，没有找到测试时也算失败。
   - 测试全部离线运行，见 [bot 使用说明](bot.md) 的“测试”一节。

在标签上运行时，配置之后会检查标签与 `CMakeCache.txt` 中的 `CMAKE_PROJECT_VERSION` 一致。这个值由 CMake 自己解析，不依赖版本标签工作流用的文本解析。

`frontend` 任务检查 `frontend/` 中的 [管理后台](admin.md)。它与矩阵任务并行，在 `ubuntu-24.04` 上以 `frontend/` 为工作目录运行，不需要 CMake 和 vcpkg：

1. 用 `pnpm/action-setup` 安装 `frontend/package.json` 的 `packageManager` 指定的 pnpm 版本。
2. 用 `actions/setup-node` 安装 Node.js 24（`NODE_VERSION`），同时缓存 pnpm store，缓存键基于 `frontend/pnpm-lock.yaml`。
3. `pnpm install --frozen-lockfile`：lockfile 与 `package.json` 不一致时任务失败。
4. `pnpm check`：`vue-tsc` 类型检查、不允许警告的 `oxlint` 和 ESLint，以及对 `src/` 的 `prettier --check`。
5. `pnpm build-only`：只做 Vite 构建，不再重复类型检查。

只有 assistantbot 有这个任务，两个插件库没有前端。

其他约定：

- 测试任务只需要仓库只读权限，`actions/checkout` 不保留凭据。
- 第三方 action 固定到提交 SHA，并在注释中保留对应版本；更新时同时核对版本与提交。
- 升级 CMake 时，必须同时更新版本与校验和。
- 升级 vcpkg 提交会改变 drogon 等依赖的版本。应先在本地用同一提交验证，再同步更新 [AGENTS.md](../../AGENTS.md) 中记录的 drogon 版本。

## 本地验证

在仓库根目录执行：

```sh
actionlint .github/workflows/linux-ci.yml .github/workflows/version-tag.yml .github/workflows/docs.yml
just test
just test release
just frontend-check
just frontend-build
```
