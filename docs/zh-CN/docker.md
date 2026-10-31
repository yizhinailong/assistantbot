# Docker

[English](../en/docker.md) · 简体中文

[文档索引](README.md) · [项目首页](../../README.zh-CN.md)

## 用 Compose 启动

需要 Linux 上的 Docker Engine、BuildKit 和 Docker Compose 2.24 或更新版本。Compose 使用主机网络，与 `scripts/docker_run_napcat.sh` 和 `config.json` 中的回环地址一致。NapCat 独立运行，按 [bot 使用说明](bot.md) 配置其 OneBot HTTP 服务器和带签名的 HTTP 客户端。

在仓库根目录执行：

```sh
cp .env.example .env
```

编辑 `.env`，填写四个变量：

| 变量 | 值 |
| --- | --- |
| `ONEBOT_ACCESS_TOKEN` | NapCat 的 OneBot HTTP 服务器 token |
| `ONEBOT_SECRET` | NapCat 的 HTTP 客户端用于事件签名的 token |
| `DEEPSEEK_API_KEY` | 默认配置指定的模型 API key |
| `ASSISTANTBOT_ADMIN_TOKEN` | 自己设置的管理员登录令牌 |

包含字面 `$` 的值用单引号包裹。Compose 读取 `.env`，已有的 shell 环境变量优先。可选模型角色和 MCP 服务指定的环境变量也可以放进 `.env`，一并传入容器。Git 忽略 `.env`，镜像构建上下文也排除它；四个必需变量未设置或为空时，Compose 会在创建容器前报错。

```sh
docker compose up --detach --build
docker compose ps
docker compose logs --follow assistantbot
```

打开 `http://127.0.0.1:8080`，使用 `ASSISTANTBOT_ADMIN_TOKEN` 登录。默认配置中，NapCat API 位于 `http://127.0.0.1:3000`，事件回调地址为 `http://127.0.0.1:8080/onebot/event`。启动容器前，需先停止占用 8080 端口的本机 bot 进程。

首次构建需要下载工具链并编译依赖。BuildKit 会缓存 pnpm 包、vcpkg 下载和二进制包、CPM 源码及 C++ 编译结果。默认并行度为 2，在 `.env` 中设置 `BUILD_JOBS` 可调整 Compose 构建的并行度。只构建镜像时无需提供运行时密钥：

```sh
docker build --tag assistantbot:local .
# 内存足够时，可以提高编译并行度。
docker build --build-arg BUILD_JOBS=4 --tag assistantbot:local .
```

对应的快捷命令是 `just docker-build`、`just docker-up`、`just docker-down`、`just docker-logs`。`docker-build` 直接构建镜像，`docker-up` 通过 Compose 构建并启动。

下载依赖需要使用主机 HTTP 代理时，可以传入 Docker 预定义的 `HTTP_PROXY`、`HTTPS_PROXY` 构建参数，代理地址需能从构建环境访问。代理只监听 Linux 主机回环地址时：

```sh
docker build --network=host --build-arg HTTP_PROXY --build-arg HTTPS_PROXY \
  --tag assistantbot:local .
docker compose up --detach --no-build
```

命令读取 shell 中已有的代理环境变量。模型密钥和 bot 令牌仍只在运行时传入，不作为构建参数。

## 配置与数据持久化

Compose 将 `config.json` 只读挂载到 `/app/config.json`，镜像本身也包含一份仓库配置，供不用 Compose 时使用。需要单独保存本地配置时：

```sh
cp config.json config.local.json
```

在 `.env` 中设置 `ASSISTANTBOT_CONFIG=./config.local.json`，然后编辑该文件，Git 会忽略它。若修改环境变量名称，也需同步调整 Compose 中的必需变量。后台修改的设置仍保存在 SQLite，不会重写任一配置文件。

| 容器路径 | 用途 |
| --- | --- |
| `/app/config.json` | 只读的启动配置 |
| `/app/data` | SQLite 数据库，使用 Compose 的 `assistantbot-data` 卷 |
| `/app/frontend/dist` | 镜像内置的后台构建产物 |
| `/tmp` | 临时文件，Compose 使用 64 MiB tmpfs |

`data_dir` 应保持为相对路径 `data` 或绝对路径 `/app/data`；改用其他路径时，需同步修改可写卷的挂载位置。上传目录应位于 `/tmp` 或其他可写挂载中。

进程以 UID/GID `10001:10001` 运行，根文件系统只读。Docker 创建新命名卷时会沿用数据目录的所有权；自行绑定数据目录时，需保证该 UID 可写。默认卷与本机运行时的 `./data` 分离；迁移已有数据库时，应先停止写入，再复制一致的数据副本。

修改 `.env` 或启动配置后，重新创建容器：

```sh
docker compose up --detach --force-recreate
```

更新源码后执行 `docker compose up --detach --build`。后台修改的运行时设置仍按原有逻辑热更新。重启会清空模型上下文、登录会话和内存日志；设置、消息、用量、人物、记忆、素材和任务保留在数据卷中。

停止服务并保留数据：

```sh
docker compose down
```

`docker compose down --volumes` 会同时删除持久化卷。容器仍存在时，可停机复制整个数据目录来备份：

```sh
docker compose stop assistantbot
docker compose cp assistantbot:/app/data ./backup
docker compose start assistantbot
```

## 网络与健康检查

主机网络让容器共享 Linux 主机的回环接口，不设置 `ports` 映射；监听地址和端口由 `config.json` 决定，默认仍为 `127.0.0.1:8080`。远程访问后台时，在主机上使用带 TLS 的反向代理。

镜像每 30 秒请求一次 `GET /api/admin/health`，检查 bot 的 HTTP 服务，不检查 NapCat 是否在线、模型密钥或模型额度。修改监听端口时，也需在 Compose 覆盖配置中修改健康检查 URL。Compose 按 10 MiB 轮转容器日志，保留三个文件。

提供的 Compose 文件面向 Linux 主机网络。改用桥接网络时，需要配置 `0.0.0.0` 监听、将后台端口发布到指定的主机接口，并填写容器能够访问的 NapCat API 地址。NapCat 的回调也需指向 bot 发布后的端口；容器里的回环地址指容器自身。

## 镜像构建

前端阶段使用 Node.js 24 和项目指定的 pnpm 构建后台页面。原生构建阶段使用 Ubuntu 24.04、Clang 21、libstdc++ 14、校验过 SHA-256 的 CMake 4.2.3，以及与 Linux CI 相同的 vcpkg 提交。vcpkg 编译带 SQLite 的 Drogon 和 C++ Lua 运行时，CPM 获取 `CMakeLists.txt` 中固定的依赖库发布标签。

运行阶段只保留去掉调试符号的 release 程序、网页、默认配置、CA 证书和运行库。`SSL_CERT_FILE`、`SSL_CERT_DIR` 将链接的 OpenSSL 客户端指向运行环境的系统证书。不复制构建工具或密钥。Dockerfile 为 `amd64` 和 `arm64` 选择经过校验的 CMake 归档；运行验证在 `amd64` 上进行。

使用 stdio 的 MCP 服务需要在运行镜像中安装其可执行程序及解释器。可在派生镜像中添加，并将服务路径设置为容器内的路径；前端构建阶段使用 Node.js，运行镜像中没有 Node.js。HTTP MCP 服务只需提供可访问的 URL。

Docker 参考文档：[多阶段构建](https://docs.docker.com/build/building/multi-stage/)、[构建缓存](https://docs.docker.com/build/cache/optimize/)、[Compose 服务](https://docs.docker.com/reference/compose-file/services/)。
