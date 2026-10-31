# TODO

管理后台功能清单，全部实现后删除本文件。对照的是 InSoulForge `v1.7.0-beta.8`（commit `cc26f59`）的管理后台。它的功能与实现整理在工作区根目录的 `insoulforge-features.md` 中（不在本仓库），下文的 §n 指那份文档的章节。

现状：`frontend/` 是 Vue 3.5、Vite 8、TypeScript 6 的应用，用 pnpm 管理，已完成认证、运行状态首页、访问控制、会话、回复设置、用量统计和日志，提供实时聊天记录、上下文重置、设置热更新、UTC 用量图表和日志过滤。后端接口在 `src/controllers/admin/`。前后端衔接已完成，使用说明见 [管理后台](docs/zh-CN/admin.md)。bot 只回复私聊好友，访问策略保存在 SQLite，回复设置的默认值在 `config.json`，后台覆盖值、聊天记录和用量保存在 SQLite；模型上下文与日志缓冲只在内存里。

约定：

- 后台接口都放在 `/api/admin/` 下，用 `ADD_METHOD_TO` 写绝对路径（InSoulForge 用的是 `/admin/api/`）。
- 密钥只放在环境变量里，接口不返回，页面也不能修改。`config.json` 是公开仓库里的文件，后台改的设置不写回这个文件。
- 页面需要的 OneBot 动作用 drogon-onebot 的类型化接口（v0.4.0 已有 `GetFriendList`、`GetLoginInfo`、`GetStatus`、`GetVersionInfo`）。库里没有的，先加进库并发版。
- 避开 InSoulForge 的已知问题（§21）：设置用不可变快照加原子替换，不在读取时裸读可变字段；聊天记录逐条追加写入，不在退出时整表覆盖。

基础后台功能已完成；请求追踪、群聊及依赖 bot 新能力的页面继续按下文约束保留。

## 前后端衔接

- [x] 清理脚手架：删掉 `HelloWorld`、`TheWelcome`、`WelcomeItem`、`icons/`、`logo.svg` 和 `base.css`，把 `App.vue` 改成侧栏导航布局，`frontend/README.md` 改成指向使用说明
- [x] 路由：加 `vue-router`，用 `createWebHashHistory`，这样刷新子页面时 drogon 不必回退到 `index.html`
- [x] 开发代理：在 `vite.config.ts` 的 `server.proxy` 中把 `/api` 转到 `http://127.0.0.1:8080`，WebSocket 也要转。`pnpm dev` 和 `just run` 分开运行
- [x] 接口封装：`src/api/client.ts` 的 `request<T>()` 统一处理 JSON 和 `{"error": "..."}` 错误体，失败时抛出带状态码的 `ApiError`；各接口的函数和类型放在 `src/api/admin.ts`
- [x] 托管构建产物：`config.json` 的 `document_root` 改为 `frontend/dist`，`upload_path` 改为绝对路径 `/tmp/assistantbot/uploads`；没有构建产物时 `/` 返回 404，bot 照常运行
- [x] `just` recipe：`frontend-init`、`frontend-dev`、`frontend-build`、`frontend-check`、`frontend-fix`。`just publish` 先构建网页，再由 CMake 安装到 `publish/assistantbot/frontend/dist/`。这些差异已写进 `AGENTS.md` 和工作区的 `CLAUDE.md`
- [x] CI：`linux-ci.yml` 新增 `frontend` 任务（Node.js 24），依次执行 `pnpm install --frozen-lockfile`、`pnpm check` 和 `pnpm build-only`。这是 assistantbot 独有的差异
- [x] 格式化和 lint：按 create-vue 3.24 的 `--eslint --prettier` 配置加入 oxlint、ESLint 和 Prettier。`pnpm check` 不允许警告，并检查格式，作用相当于 C++ 的 `clang-format --dry-run -Werror`
- [x] 文档：`AGENTS.md` 加前端一节；新增 `docs/en/admin.md` 和 `docs/zh-CN/admin.md`，并加进两份 README 和文档索引；`docs/*/ci.md` 写明 `frontend` 任务

## 运行时数据

后台修改的设置、聊天记录和用量都要落盘，但不能写进 `config.json`。存储用 SQLite，由插件 `storage::Database`（`src/storage/`）负责，后面几节都依赖它。

- [x] 接入 SQLite：`just init` 和 CI 改为安装 `drogon[sqlite3]`，CI 的 vcpkg 缓存键随之改变；drogon 没有 SQLite 支持时 CMake 配置直接报错。客户端只有一个连接，运行在独立线程上。打开前先用 SQLite 自己试开一次文件，因为 drogon 1.9.13 打不开文件时会直接中止进程
- [x] 数据目录：`storage::Database` 的 `data_dir` 键，默认 `data`，目录不存在时自动创建，已加进 `.gitignore`。依赖 app 的测试每次先清空 `/tmp/assistantbot-test/data`，其他测试各用一个临时目录
- [x] Schema 版本：`PRAGMA user_version` 记录已执行的迁移数（§17），每个迁移和版本号在同一个事务里提交。新数据库从 0 开始执行全部迁移，不另外维护一份完整建表语句；遇到比当前程序更新的数据库时拒绝启动。版本 1 只有 `settings` 表；运行状态功能追加的版本 2 新增 `model_usage` 表，版本 3 新增 `chat_messages`，版本 4 增加缓存命中字段
- [x] 设置快照：`workflow::BotSettings` 启动时把 `settings` 表中 `bot` 一行的覆盖值叠加到 `config.json` 上，得到 `std::shared_ptr<BotConfig const>`。`Update` 校验通过并写入数据库后才替换快照，保存过程中再次保存会被拒绝；`PrivateChat` 每次回复开始时取一次快照（§16.2、§21.1）

## 认证

InSoulForge 每次启动时随机生成令牌并打印到日志里（§18.1）。在本项目中令牌属于密钥，按约定放在环境变量里，也不打印。监听地址保持 `127.0.0.1`；需要远程访问时，在前面加反向代理和 TLS，不在 bot 里实现。

- [x] 令牌：config.json 只写变量名（例如 `admin_token_env: "ASSISTANTBOT_ADMIN_TOKEN"`），变量未设置或为空时启动失败，与两个库的 `*_env` 键行为一致。读取要用新加的 `read_env` 辅助函数（Windows 下用 `_dupenv_s`）
- [x] 登录：`POST /api/admin/auth/login` 提交 `{token}`，用常量时间比较。成功后写入 `HttpOnly`、`SameSite=Strict` 的会话 cookie。另有 `GET auth/status` 和 `POST auth/logout`
- [x] 鉴权：用 filter 或 pre-routing advice 拦截 `/api/admin/` 下的路由和 WebSocket，未登录时返回 401。`auth/login`、`auth/status` 和 `health` 不拦截
- [x] 登录页：`request()` 收到 401 时跳到登录页，登录后回到原来的页面

## 运行状态（首页）

- [x] `GET /api/admin/status`：版本（`project(VERSION)` 通过编译定义传进代码）、启动时间、bot 的 QQ 号和昵称（`GetLoginInfo`）、NapCat 是否在线（`GetStatus`）及其版本（`GetVersionInfo`）、模型名、当前会话数
- [x] 暂停和恢复：`GET/POST /api/admin/bot-status`。暂停期间 `handle_message` 丢弃所有消息，既不排队也不回复（§10.1）。暂停状态只存在内存里，重启后恢复运行
- [x] 进程指标：每 60 秒采样一次 RSS、峰值 RSS 和线程数，保留最近 24 小时（1440 个点），通过 `GET /api/admin/process-metrics` 读取（§19）
- [x] 页面：显示上述信息、当天的用量概览和内存曲线

## 访问控制

默认回复所有未被拦截的好友，可以在后台限制为允许名单；策略保存在 SQLite 的 `access` 设置行，重启保留。

- [x] 允许名单：分"所有好友"和"只回复名单内的好友"两种模式，默认保持现在的"所有好友"。接口是 `GET/PUT /api/admin/access`（模式）和 `GET/POST/DELETE /api/admin/allowlist[/{qq}]`。名单外的消息记日志并保存聊天记录，不回复
- [x] 黑名单：`GET/POST/DELETE /api/admin/blacklist[/{qq}]`。命中后不回复，也不执行 `/help`、`/reset`，优先级高于允许名单（§8.2）
- [x] 判断位置：`handle_message` 在筛出私聊好友之后、处理命令之前查名单。名单从内存快照读取，不必每条消息都查一次存储
- [x] 好友列表：`GET /api/admin/friends` 调用 `GetFriendList`，返回昵称、备注，并标出在哪个名单里
- [x] 页面：好友表格，可以直接把好友加入或移出两个名单；支持搜索、按名单筛选和手动输入 QQ 号

## 会话

`SessionStore` 按好友保存最近 `history_turns` 轮对话，只在内存里。

- [x] 会话列表：`GET /api/admin/sessions` 返回每个好友的 QQ 号、昵称、轮数、最后活动时间，以及是否在等模型回复。为此 `SessionStore` 要加一个在锁内复制状态的快照方法
- [x] 会话详情：`GET /api/admin/sessions/{qq}` 返回记录下的各轮对话
- [x] 重置：`POST /api/admin/sessions/{qq}/reset`，效果与私聊里的 `/reset` 相同（`PrivateChat::Reset`）
- [x] 聊天记录：收到和发出的每条消息逐条追加写入存储，失败的回复也要记录；用 `GET /api/admin/sessions/{qq}/messages` 分页查询。模型上下文仍然来自 `SessionStore`，重启后是否从聊天记录恢复上下文另行决定
- [x] 实时推送：`/api/admin/ws`（drogon 的 `WebSocketController`）推送新消息，客户端可以只订阅某个好友（§18.3）
- [x] 页面：会话列表、实时追加的聊天记录、重置按钮

## 回复设置

这些设置以 config.json 中 `controllers::AssistantBot` 的配置为默认值，后台覆盖值保存在 SQLite，保存后热更新。

- [x] 读写：`GET/PUT /api/admin/bot-config`，包括 `system_prompt`、`history_turns`、`idle_reset_minutes`、`request_timeout`、`max_tokens`、`max_reply_chars`。用 `BotSettings::Overrides` 和 `Update`（需要让 `controllers::AssistantBot` 公开它的 `BotSettings`），`std::invalid_argument` 的信息原样返回给页面，保存冲突返回 409
- [x] 热更新：`PrivateChat` 已经每次回复取一次快照。`SessionStore` 的历史和空闲限制也已改成每次从设置快照读取
- [x] 恢复默认：`Update({})`，回到 config.json 里的值
- [x] 提示词：`system_prompt` 用多行编辑框，并显示默认的 `DEFAULT_SYSTEM_PROMPT`
- [x] 模型：页面可以修改模型名，请求时填入 `ChatCompletionCreateParams::model`，不改 drogon-openai 插件的配置。依赖已固定到 drogon-openai v0.2.0；本页不修改 `base_url`。API key 不在页面上出现
- [x] 页面：设置表单，带保存和恢复默认按钮

## 用量统计

首页已逐条保存模型请求结果及三个令牌总数（`storage::record_usage`），并在 `/api/admin/status` 返回当天 UTC 汇总；下列任务继续扩展缓存命中数、完整查询和推送。

- [x] 记录：每次模型请求后记下 QQ 号、模型、`prompt_tokens`、`completion_tokens`、`total_tokens` 和缓存命中数。使用 drogon-openai v0.2.0 的 `prompt_tokens_details.cached_tokens`，DeepSeek 的 `prompt_cache_hit_tokens` 在 `usage.raw` 里
- [x] 查询：`GET /api/admin/usage?from=&to=` 返回总计、当天、按天和按好友的统计。时间统一存成 UTC 时间戳，按天分组时用同一个时区换算，避免 §21.1 中的时区偏差
- [x] 推送：记录用量后通过 `/api/admin/ws` 广播 `usage_updated`
- [x] 页面：按天的柱状图和按好友的排行

## 日志

- [x] 日志缓冲：用 `trantor::Logger::setOutputFunction` 在原有输出之外，把日志另存一份到内存环形缓冲（例如最近 5000 条）
- [x] 实时日志：`/api/admin/logs/ws` 推送日志，客户端可以按等级、关键词、QQ 号过滤；`GET /api/admin/logs` 读取缓冲中已有的日志
- [x] 日志等级：`PUT /api/admin/log-level` 在运行时调用 `trantor::Logger::setLogLevel`
- [x] 页面：日志列表，支持自动滚动、暂停和过滤

## 请求追踪

> 先不实现，需要两个库先提供钩子

InSoulForge 保存最近 50 条模型和 OneBot 的 HTTP 请求与响应（§19）。本项目的 HTTP 请求都由两个库发出，所以要先在库里加回调，再由 bot 保存。drogon-openai 的 `OPENAI_LOG` 只把请求写进日志。

- [ ] drogon-openai 和 drogon-onebot 的请求回调：方法、URL、状态码、耗时，以及截断后的请求体和响应体，去掉 `Authorization`
- [ ] `GET/DELETE /api/admin/http-traces`，并支持下载为 JSON

## 群聊

> 先不实现，bot 支持群聊后再加

- [ ] 会话启用：群默认不启用。`GET /api/admin/groups` 调用 `GetGroupList`，可以启用和禁用（§8.1）
- [ ] 每个群的开关：自动回复、是否把群名片发给模型、群命令（§8.1）
- [ ] 每个群的附加提示词（§15.4）
- [ ] 管理员：只用于群命令的权限判断（§7、§8.2）

## 依赖 bot 新功能的页面

> 先不实现，对应功能进入 bot 后再加

- [ ] 表情库：收藏表情列表和描述（§4.3）。用到 NapCat 扩展动作，要先加进 drogon-onebot
- [ ] 人物信息与记忆：会话外号、长期记忆、人物档案、好感度（§12）
- [ ] 定时任务：按会话列出和取消（§14.1）
- [ ] 工具配置：内置工具的开关和描述、Lua 自定义工具（§11）
- [ ] MCP 服务（§11.5）
- [ ] 多个模型角色：回复判断、深度思考、图片识别、记忆、embedding 等（§15.1），以及执行配置（最大工具轮数）
- [ ] 角色形象图（§13.4）

## 验证

- 后端：每组接口写一个 `tests/test_admin_*.cpp`，像 `test_health` 一样跑在 `tests/config.json` 的应用上（端口 18082），覆盖正常结果、参数错误和未登录时的 401。存储使用临时目录。
- 访问控制和暂停：在 `test_private_chat` 里用假 NapCat 验证名单外、黑名单内和暂停期间都不回复。
- 前端：`pnpm type-check`、`pnpm build` 和 lint 都进 CI。接口封装和关键组件可以用 Vitest 测试。
- 手动：同时运行 `just run`、`scripts/chat.py` 和 `pnpm dev`，在页面上确认消息实时出现，改设置后下一条回复按新值生效。
