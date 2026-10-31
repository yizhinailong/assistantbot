# 管理后台

[English](../en/admin.md) · 简体中文

[文档索引](README.md) · [项目首页](../../README.zh-CN.md)

bot 带有一组用于管理它的网页，位于 `frontend/`。网页用 Vue 3、Vite 和 TypeScript 编写，通过 `/api/admin/` 下的 HTTP 接口访问 bot。运行状态首页显示账号、NapCat 状态、模型、会话数、当天模型用量和进程资源；其他页面管理访问控制、实时会话、回复设置、UTC 用量统计和日志。

登录后才能查看运行数据和使用控制功能。请像 `config.json` 那样，让 bot 只监听 `127.0.0.1`；需要远程访问时，在前面加反向代理和 TLS。

## 认证

启动前，把 `ASSISTANTBOT_ADMIN_TOKEN` 设置为自己的非空密钥。`admin::Auth` 插件在 `config.json` 的 `admin_token_env` 中只保存变量名，变量未设置或为空时启动失败。令牌最多 4096 字节，通过固定长度的 SHA-256 摘要进行不提前退出的比较，既不写日志，也不通过接口返回。

打开 `/#/login` 并输入令牌。登录成功后，服务设置仅限当前主机、路径为 `/api/admin` 的 `HttpOnly`、`SameSite=Strict` cookie。浏览器只保存随机会话 ID，页面不把令牌放进本地存储或 URL。会话在 12 小时后过期，退出登录立即撤销，重启后全部失效。每次登录会替换浏览器原来的会话；服务最多保留 256 个会话，达到上限时移除最早的会话。

`/api/admin/` 下的请求（包括 WebSocket 升级）都需要会话，仅 `health`、`auth/login` 和 `auth/status` 例外。未认证的请求返回 HTTP 401 和 JSON 错误。页面收到 401 会跳到登录页，登录后回到原来的页面。后台响应都带有 `Cache-Control: no-store`。

## 运行状态首页

- `/#/` 显示编译时的项目版本、启动时间、配置的模型，以及已经创建内存会话的用户数（包括空会话和重置后的会话）。页面通过类型化的 `GetLoginInfo`、`GetStatus`、`GetVersionInfo` 并发查询 NapCat，每项超时为 2 秒；失败的项目显示未知，其他数据仍可查看。
- 暂停会在命令处理和入队前丢弃新的好友消息，包括 `/help` 和 `/reset`。已经开始的回复（包括此前排队的提示词）仍正常完成。恢复后接受新消息，暂停期间丢弃的消息不重放。被忽略的私聊好友消息仍保留在聊天记录中。暂停标记只在内存中，重启后恢复运行。
- 当天用量统计已完成的模型请求数、可用回复数，以及接口返回的输入、输出和总令牌数。每次请求逐条写入 SQLite，重启不丢失累计值。“当天”以 UTC 零点划分，接口也返回统计边界。未返回令牌数的请求单独计数，不计入令牌总量。命令不调用模型，不计为请求。存储失败会记日志，仍继续发送回复。
- 启动时和每隔 60 秒采集进程 RSS、峰值 RSS 和线程数，按时间顺序在内存中保留最近 1440 个样本，重启后清空。Linux 读取 `/proc/self/status`，Windows 使用进程 API；无法获取的测量值返回 `null`。内存曲线支持指针选择、方向键和数据表。
- 页面每隔 60 秒自动刷新，也可以手动刷新。

## 访问控制

打开 `/#/access`，或点击侧栏的“访问控制”：

- **所有好友**（`all_friends`，默认）回复所有未在黑名单中的私聊好友。**只回复允许名单内的好友**（`allowlist`）只回复名单内的好友；允许名单为空时不回复任何好友。
- 黑名单在两种模式下都优先，即使同一个 QQ 号也在允许名单中。被拦截的消息只记日志，不执行命令、不发送文字提示，也不调用模型或入队。被拦截的 `/reset` 不会清空历史。群消息和临时会话始终忽略。
- 修改回复范围后点击保存。好友表格中的按钮会立即把对应 QQ 号加入或移出所选名单。可以按 QQ 号、昵称或备注搜索，也可以按名单筛选。NapCat 无法提供好友列表时，仍可手动输入 QQ 号，并管理仅在名单中的记录。
- 模式和两个名单一起保存在 SQLite `settings` 表的 `access` 行。保存成功后用于新消息，重启后保留。此前已接受的工作继续处理，被拦截的消息不重放。切换模式保留两个名单；保存失败保留当前策略；同时保存返回 409，可稍后重试。数据库中的策略无效时拒绝启动。
- 好友数据通过类型化的 `GetFriendList` 获取，超时 2 秒。502 表示 NapCat 未能提供列表，本地访问设置接口仍可使用。表格中的回复状态只表示名单权限；暂停期间仍不会回复新消息。

## 会话与聊天记录

打开 `/#/sessions`，可以搜索好友、查看最近的模型对话轮次、加载更早的消息并重置上下文。列表显示 QQ 号、昵称、保留轮数、最后活动时间和是否正在等待回复。重启后仍能看到有聊天记录的好友，其内存轮数为零。

每条收到的私聊好友或群聊消息都单独追加到 SQLite，包括命令、媒体占位文本，以及被访问策略或暂停拦截的消息。发出的每个分段分别记为 `sent` 或 `failed`，失败时保留发送错误；模型失败时发出的提示也会保存。记录使用服务端 UTC Unix 时间戳和十进制字符串 ID。临时私聊会话只记日志。存储失败会记日志，不阻断回复。

模型上下文仍只保存在内存，不从聊天记录重建。页面重置调用与 `/reset` 相同的 `PrivateChat::Reset`，清空上下文和待处理消息；正在生成的回复仍会完成，但不会写回已清空的上下文。重置保留聊天记录，页面操作不会向 QQ 发送确认消息。

消息接口默认返回最近 50 条，支持 `limit=1..200`，结果包括按时间顺序排列的 `messages`、`has_more`、`next_before` 和 `next_after`。传入不包含边界的 `before` ID 读取更早记录，或用 `after` ID 补取断线期间的记录，两者不能同时使用。页面按 ID 合并去重实时与历史消息，重新连接后补取缺失消息。未知会话的详情和重置返回 404，查询其消息返回空页。

## 回复设置

打开 `/#/settings`，可以修改系统提示词、模型名、历史轮数、空闲重置间隔、请求超时、最大输出令牌和每条消息字数。多行提示词编辑框同时展示内置的 `DEFAULT_SYSTEM_PROMPT`。提示词留空表示不发送系统消息；模型名留空使用 OpenAI 插件默认模型；最大令牌留空表示不发送限制。

`GET /api/admin/bot-config` 返回当前 `values`、已保存的 `overrides`、config.json 中的 `defaults` 和 `default_system_prompt`。`PUT` 整体替换覆盖对象，未包含的键回到 config.json，`{}` 恢复默认。`max_tokens: null` 可以清除配置中的限制。校验失败返回 400，保留 `std::invalid_argument` 的原始信息；同时保存返回 409；存储失败返回 500，保留当前快照。

保存的修改重启后保留，从下一次回复生效。SessionStore 每次操作读取最新的历史和空闲限制；缩小历史轮数后，在下次读取历史、入队或写入时裁掉多余轮次。已经开始的请求继续使用原来的提示词、模型、超时、令牌和回复分段设置。模型覆盖值只填入每次请求的 `ChatCompletionCreateParams::model`，服务商配置仍在 config.json 中；这里不返回或编辑 API key。

## 用量统计

打开 `/#/usage`，查看每日令牌柱状图、数据表、好友排行，以及总计和当天计数。日期输入包含起止两天，统一使用 UTC。失败请求和未返回用量的请求仍计数，令牌总数只累计实际返回的值。

`GET /api/admin/usage?from=&to=` 使用 UTC Unix 秒，包含 `from`、不包含 `to`，默认最近 30 个 UTC 日（含当天），区间必须为正且不超过 366 天。结果包含 `total`、独立于所选区间的 `today`、以零点 `timestamp` 标识的 UTC `days`，以及按十进制字符串 `user_id` 分组的 `users`。全部分组来自同一次 SQLite 读取快照，避免并发追加时各部分不一致。

每次模型请求完成后，保存 QQ 号、模型、成功状态、输入/输出/总令牌和可选的缓存命中数。drogon-openai v0.3.0 提供 `prompt_tokens_details.cached_tokens`，没有该字段时读取 DeepSeek 的 `usage.raw.prompt_cache_hit_tokens`。未知缓存报告保存为 SQL NULL，明确报告零命中也计入 `cached_requests`。Schema 3 新增聊天记录，Schema 4 新增缓存命中字段，保留原有设置和用量。用量保存成功后广播 `usage_updated`，页面重新读取当前所选区间。

## 日志

打开 `/#/logs`，查看最近 5000 条运行日志、暂停显示、自动滚动，并按最低等级、区分大小写的关键词或完整 QQ 号过滤。应用过滤条件时读取最近 200 条匹配记录。继续显示和重新连接时从最后检查的 ID 补取；缓冲已淘汰部分记录时用 `truncated` 提醒。浏览器也最多显示 5000 条。

后台日志副本串接 Trantor 原有的输出和刷新回调，控制台和文件输出继续工作；副本会遮蔽已知的后台、OneBot 和模型服务密钥。在监听器接收请求前开始捕获，关闭时停止，缓冲只保存在内存。WebSocket 每秒为每个连接发送最多 200 条，避免在日志回调中直接推送而递归产生日志。

`GET /api/admin/logs` 接受 `level`、`keyword`（最多 256 字节）、`qq`、`limit=1..5000`（默认 200）和可选、不含边界的 `after`，返回 `entries`、`cursor`、`oldest_id`、`truncated`、当前 `log_level` 和规范化的 `filters`。不传 `after` 时读取最近的匹配项；传入后向前扫描，未匹配的记录也推进游标。日志 ID 是进程内的十进制字符串。`PUT /api/admin/log-level` 只接受 `{level: "TRACE"|"DEBUG"|"INFO"|"WARN"|"ERROR"|"FATAL"}`，调用 `trantor::Logger::setLogLevel` 控制后续日志，重启后使用 config.json 的等级。

## 实时协议

两个 WebSocket 都使用后台会话 cookie，连接后发送 `{"type":"ready"}`。会话过期、退出登录、登录轮换或淘汰会话时关闭相应连接。浏览器升级请求的 Origin 必须匹配请求主机；客户端重新连接前检查登录状态，并恢复订阅。

- `/api/admin/ws` 默认接收全部应用事件。发送 `{"type":"subscribe","user_id":"12345"}` 选择一个好友，传入 `null` 恢复全部好友，确认事件为 `subscribed`。`message_created` 带有 `user_id` 和一条已落盘的 `message`；`session_updated` 提示客户端重新读取会话状态。好友筛选作用于这两类事件；`usage_updated`（附请求的 `user_id`）和 `settings_updated` 广播给所有应用事件订阅者。
- `/api/admin/logs/ws` 在收到 `{"type":"subscribe","level":"WARN","keyword":"","qq":"12345","after":"123"}` 后开始发送。过滤条件和 `after` 都可省略，`after` 是十进制字符串游标。确认和实时批次都使用 `type: "logs"`，其余字段与 GET logs 相同。无效订阅返回 `{type: "error", error: "..."}`，保留原订阅。

## 环境要求

| 工具 | 版本 | 说明 |
| --- | --- | --- |
| Node.js | `^22.18.0` 或 `>=24.12.0` | 即 `frontend/package.json` 中 `engines` 的范围；CI 使用 Node.js 24 |
| pnpm | 12.15.0 | 由 `frontend/package.json` 的 `packageManager` 指定。可以用 `npm install -g pnpm@12.15.0` 安装，也可以交给 Corepack 选择 |

bot 本身的构建和运行不需要它们。没有构建网页时，bot 对 `/` 返回 404，其他功能照常工作。

在仓库根目录安装网页的依赖。首次需要安装，`frontend/pnpm-lock.yaml` 变化后再安装一次：

```sh
just frontend-init
```

## 开发

在两个终端里分别运行 bot 和 Vite 开发服务器：

```sh
just run            # bot 监听 127.0.0.1:8080，环境变量见 bot 使用说明
just frontend-dev   # 网页地址为 http://localhost:5173/
```

Vite 提供带热更新的网页，并把 `/api`（包括 WebSocket）转发到 `http://127.0.0.1:8080`。修改 bot 的监听地址时，也要同步修改 `frontend/vite.config.ts` 中的 `server.proxy`。

## 由 bot 提供网页

```sh
just frontend-build
just run
```

`just frontend-build` 先做类型检查，再把网页输出到 `frontend/dist/`。`config.json` 把 `document_root` 设为 `frontend/dist`，drogon 按工作目录解析这个路径，`just run` 的工作目录是仓库根目录。这样 bot 就在 `http://127.0.0.1:8080/` 提供网页。

- 网页使用 hash 路由（`/#/...`），所有页面都从 `/` 加载，drogon 不需要回退路由。
- `upload_path` 是绝对路径 `/tmp/assistantbot/uploads`。drogon 启动时会创建这个目录，如果用相对路径，它会被建在 `frontend/dist/` 里面。

`just publish` 也会构建网页，并把它们安装到可执行文件旁边的 `publish/assistantbot/frontend/dist/`。在 `publish/assistantbot/` 中放一份 `config.json` 并从这里运行 bot，同一个 `document_root` 就能找到网页。

## 接口

网页通过 `frontend/src/api/client.ts` 中的 `request()` 访问 bot，每个路由在 `frontend/src/api/admin.ts` 中对应一个带类型的函数。

| 路由 | 响应 |
| --- | --- |
| `GET /api/admin/health` | `{"status": "ok"}` |
| `POST /api/admin/auth/login` | 请求体 `{token}`；返回 `{authenticated: true}` 和会话 cookie，参数错误为 400，令牌错误为 401 |
| `GET /api/admin/auth/status` | `{authenticated: boolean}`，无需登录 |
| `POST /api/admin/auth/logout` | 撤销当前会话并使 cookie 过期 |
| `GET /api/admin/status` | `version`、`started_at`（Unix 秒）、`model`、`session_count`、`paused`、`bot`（字符串 `user_id`、`nickname`）、`napcat` 和 `today_usage` |
| `GET /api/admin/bot-status` | `{paused: boolean}` |
| `POST /api/admin/bot-status` | 请求体 `{paused: boolean}`；返回更新后的暂停标记，参数错误为 400 |
| `GET /api/admin/process-metrics` | `interval_seconds: 60`、`capacity: 1440` 和按时间排序的 `samples`，每项含 `timestamp`、`rss_bytes`、`peak_rss_bytes` 和 `threads` |
| `GET /api/admin/access` | `{mode: "all_friends" \| "allowlist"}` |
| `PUT /api/admin/access` | 请求体只含 `{mode}`；返回保存后的模式，保留两个名单 |
| `GET /api/admin/allowlist`、`GET /api/admin/blacklist` | `{qqs: ["12345", ...]}`，按数值排序 |
| `POST /api/admin/allowlist`、`POST /api/admin/blacklist` | 请求体只含 `{qq: "12345"}`；幂等添加，返回保存后的 `{qqs}` |
| `DELETE /api/admin/allowlist/{qq}`、`DELETE /api/admin/blacklist/{qq}` | 幂等移除，返回保存后的 `{qqs}` |
| `GET /api/admin/friends` | `{friends: [{user_id, nickname, remark, in_allowlist, in_blacklist, allowed}, ...]}`；上游失败返回 502 |
| `GET /api/admin/sessions` | `{sessions: [{user_id, nickname, turns, last_active, busy}, ...]}` |
| `GET /api/admin/sessions/{qq}` | 会话元数据与按时间排列的 `history: [{user, assistant}, ...]` |
| `POST /api/admin/sessions/{qq}/reset` | 清空上下文和队列，返回更新后的详情 |
| `GET /api/admin/sessions/{qq}/messages` | 稳定游标分页，支持 `limit`、`before` 或 `after` |
| `GET /api/admin/bot-config` | `values`、`overrides`、`defaults` 和 `default_system_prompt` |
| `PUT /api/admin/bot-config` | 完整覆盖对象，`{}` 恢复 config.json 默认值 |
| `GET /api/admin/usage` | 可选 UTC `from`/`to`，返回 `total`、`today`、`days` 和 `users` |
| `GET /api/admin/logs` | 过滤后的缓冲记录和补取游标 |
| `PUT /api/admin/log-level` | 请求体 `{level}`，返回更新后的运行等级 |
| WebSocket `/api/admin/ws`、`/api/admin/logs/ws` | 上文所述的应用事件和过滤日志批次 |

访问控制接口都需要登录。参数错误返回 400，同时保存返回 409，存储失败返回 500，并提供 `error` 字段。QQ 号必须是表示正整数的十进制**字符串**，最大为有符号 64 位整数 `9223372036854775807`；不接受符号、空白、小数或 JSON 数字，前导零会被规范化。响应也使用十进制字符串，避免超出 JavaScript 精确整数范围时丢失精度。修改模式不会自动清空名单，编辑其中一个名单也不会改变另一个。

`request()` 把请求体编码成 JSON 发送，并解析 JSON 响应。空响应（例如 204）得到 `undefined`。请求失败时抛出 `ApiError`：

- `status` 是 HTTP 状态码；无法访问 bot 时为 0。
- `message` 取自 `{"error": "<message>"}` 形式的响应体，这是后台接口统一的错误格式；没有这样的响应体时，使用状态行。

## 检查

| Recipe | 在 `frontend/` 中执行 |
| --- | --- |
| `just frontend-init` | `pnpm install --frozen-lockfile` |
| `just frontend-dev` | `pnpm dev` |
| `just frontend-build` | `pnpm build`：`vue-tsc` 类型检查和 `vite build` |
| `just frontend-check` | `pnpm check`：类型检查、不允许警告的 `oxlint` 和 ESLint，以及对 `src/` 的 `prettier --check` |
| `just frontend-fix` | `pnpm lint` 和 `pnpm format`：先应用 lint 的自动修复，再用 Prettier 格式化 `src/` |

Linux CI 在单独的任务中运行 `pnpm check` 和构建，见 [版本标签与 Linux CI](ci.md)。

## 请求追踪

请求追踪页在内存中保留最近 50 次模型、OneBot、MCP 和 Lua HTTP 请求，显示方法、地址、状态、毫秒耗时和截断后的请求/响应正文。可以按来源筛选、暂停自动刷新、清空缓冲和下载当前筛选结果的 JSON。管理副本会去掉认证头、已配置的密钥、凭据字段和内联图片数据；正文仍可能包含对话内容。重启后不保留追踪。

`GET /api/admin/http-traces?source=model|onebot|mcp|tool` 读取缓冲，省略来源则读取全部；`DELETE /api/admin/http-traces` 清空；`GET /api/admin/http-traces/download` 下载 JSON，接受相同筛选。`/api/admin/ws` 用 `http_trace_created` 通知新增记录。OneBot 反向 WebSocket 动作不产生 HTTP 追踪。

## 群聊

群聊页通过类型化 OneBot 接口读取已加入的群，所有群默认关闭。每个群可以设置启用、自动回复（关闭时需提及机器人）、群名片/昵称上下文、群命令和附加系统提示词，下一条消息起生效。管理员可以使用 `/on`、`/off`、`/auto on`、`/auto off`、`/prompt 提示词`、`/help`、`/reset`；管理员名单只授予群命令权限。暂停和黑名单仍然有效，私聊允许名单不限制已启用的群。

`GET /api/admin/groups` 合并群列表与配置；`PUT /api/admin/groups/{qq}` 接收 `{values, revision}`。通用配置使用 `GET/PUT /api/admin/features/{section}`，节名为 `groups`、`admins`、`models`、`execution`、`memory`、`tools`、`mcp`。过期版本返回 409。会话接口和订阅中，正数字符串表示好友，负数字符串表示群；消息另含 `sender_id`、`group_id`。重启保留配置和聊天记录，但不从记录恢复模型上下文。

## 表情与角色图

表情与角色图页读取最多 200 个 NapCat 收藏表情，可以修改描述、添加和删除。添加时填写 **NapCat 主机上** 已存在的图片路径；NapCat 在容器里运行时，也要使用容器内路径。删除会改变 QQ 账号的收藏。这些接口使用 NapCat 扩展，其他 OneBot 实现可能返回 502 和动作错误。接口为 `GET/POST /api/admin/stickers`、`PUT/DELETE /api/admin/stickers/{res_id}`。

角色图支持最大 8 MiB 的静态 PNG、JPEG、WebP，保存在 SQLite，可供生图工具使用。`GET/POST/DELETE /api/admin/character-image` 分别读取、替换和删除；POST 接收 `{data_url, revision}`，DELETE 接受 `?revision=`，旧版本返回 409。请求正文上限设为 12 MiB，以容纳 base64 编码。

## 模型角色与执行

“模型与执行”页面配置 `router`、`decision`、`executor`、`executor_fallback`、`thinking`、`vision`、`image_generation`、`memory` 和 `embedding`。可选角色默认关闭；执行角色关闭时使用主接口和回复设置。每个角色可独立设置模型、地址、密钥环境变量名、超时、重试、令牌上限、温度和推理强度。指定自定义地址后会清除继承的认证信息，需要明确填写环境变量名。接口不返回实际密钥。

任务分析和回复判断在执行模型之前运行。深度思考和图片生成由工具调用；图片识别会描述已接受消息中的 HTTP 图片，并支持对这些原图继续提问。记忆与向量角色用于整理和检索。执行请求失败时可使用备用角色。所有模型请求，包括辅助请求和失败请求，都单独记录用量；图片请求不自动重试。

执行限制包括最大工具轮数（默认 6）、工具结果字符数（4000）、MCP 每页数量（10）、每次启用数量（4）和每次回复启用总数（12）。每轮回复使用一份功能设置快照，修改从下一轮生效。只有实际提供给模型的工具可以执行，同一个调用 ID 不会重复产生副作用；一次模型响应最多接受 16 个调用。

## 人物与记忆

“人物与记忆”页面可编辑会话外号、人物档案、关系、近期互动、好感度和长期事实。选择好友或群聊并填写 QQ 后读取会话，再编辑或新增记录。人物资料按 QQ 跨会话共享，外号和长期记忆按会话隔离。保存和删除都检查版本，旧页面不能覆盖新修改。记忆和好感度默认关闭；关闭好感度会保留已存数值，但不注入模型，也不自动更新。

启用记忆后，执行模型会收到明确标记为参考数据的上下文。可选记忆角色每隔 `maintenance_turns` 轮成功回复（默认 10）处理累积的有界对话，失败批次保留以便再次尝试；只整理已标识发送者明确说过的事实，每批好感度最多变化 ±5，总范围为 [-100, 100]。本实现不运行每日人物整理。向量角色将向量与接口地址、模型身份一起保存，检索跳过空间或维度不一致的数据，使用余弦相似度；未启用向量时按字面子串匹配。聊天记录仍不用于重启后恢复短期上下文。

| 接口 | 请求 / 结果 |
| --- | --- |
| `GET /api/admin/memory-documents/{kind}` | 列出 `aliases` 或 `people` 文档 |
| `GET/PUT/DELETE /api/admin/memory-documents/{kind}/{key}` | PUT `{value, revision}`；DELETE `?revision=` |
| `GET /api/admin/memories?session=&limit=` | 有界查询，返回维度而不返回向量 |
| `POST /api/admin/memories` | `{session, content}` |
| `PUT /api/admin/memories/{id}` | `{session, content, revision}` |
| `DELETE /api/admin/memories/{id}?revision=` | 按版本删除 |
| `POST /api/admin/memories/search` | `{session, query}`，返回相关记忆 |

## 定时任务

“定时任务”页面创建、查询和取消持久化提醒。页面输入本地时间，浏览器转换为 UTC 时间戳；每日任务固定在同一 UTC 时刻、每 24 小时重复。每个任务属于一个会话，同一会话最多保留 100 个待执行任务。`GET/POST /api/admin/tasks` 查询或创建 `{session, due_at, content, daily}`，GET 可用 `?session=` 筛选；`POST /api/admin/tasks/{id}/cancel` 只取消待执行任务，已开始执行时返回 409。

每秒轮询一次，到期后先原子认领，再把内容交给正常聊天流程。执行前重新检查暂停、私聊访问策略、发送者黑名单和群启用状态，失败保留在列表中。重启会将中断的运行任务标为失败，不重放发送；待执行任务保留。错过时间的每日任务执行一次后推进到下一个未来 UTC 时刻，避免自动重复不确定的发送。运行中的任务不支持取消。

## 内置工具与 Lua

“工具配置”页面启用工具循环，修改每个内置工具的开关和描述。`GET /api/admin/tools/builtin` 返回固定目录，包含最终回复、不回复、引用、中途回复、思考、看图、记忆、提醒、表情、图片、收藏表情管理、群提及和禁言，以及 MCP 发现与选择。群工具不会出现在私聊请求中。记忆或辅助模型关闭时，其对应工具也不提供。来源消息元数据包含当前消息 ID 和图片数；`ask_images` 使用从 0 开始的图片序号，只解析该消息里的 URL。模型工具和 Lua 宿主只向当前会话发送消息或创建任务，取消任务和删除记忆也限定当前会话；引用仅允许当前收到的消息。文本和引用工具回复遵守 `max_reply_chars`，仅第一段携带引用。即使模型参数请求其他收件人，这些限制也会生效。

Lua 工具以带版本的文档保存在 SQLite。页面支持新增、编辑、删除、JSON 导入导出、示例参数和测试。测试模拟 HTTP、发消息和提醒，只返回操作列表，不产生真实副作用。接口是 `GET /api/admin/tools/lua`、`GET/PUT/DELETE /api/admin/tools/lua/{name}` 和 `POST /api/admin/tools/lua-test`；PUT 使用 `{value, revision}`，DELETE 使用 `?revision=`，测试使用 `{name, value, arguments}`。文档字段为 `enabled`、`description`、`parameters` 和 `script`。名称是最长 48 字符的 ASCII 标识符，不能占用内置或 MCP 保留名。参数支持经过验证的 JSON Schema 子集：对象、数组、基本类型、必填字段、枚举、数值和长度范围、额外字段开关。

```lua
function run(args, ctx)
  local response = http.get("https://example.com/search?q=" .. url.encode(args.query))
  return {status = response.status, body = response.body}
end
```

每次调用创建独立受限状态。`ctx` 包含字符串 `session_id`、`user_id` 和布尔值 `is_private`。宿主提供 `http.request/get/post`、`bot.send_message`、`bot.schedule(due_at, text, daily)`；HTTP 返回 `ok`、`status`、`headers`、`body` 或 `error`，4xx/5xx 仍是有效响应，不跟随重定向。另有 `json.encode/decode/null` 和 `url.encode`。不开放文件、系统、包、调试、动态加载和直接协程接口。限制为：源码 256 KiB、Lua 分配 16 MiB、100 万指令、100 ms 活跃执行时间（不含等待宿主）、32 次宿主操作、HTTP/JSON 1 MiB、JSON 深度 32，并限制转换的总节点和文本量。标准模式匹配接口不开放，`string.find` 仅按字面查找。HTTP 可以访问 bot 所在网络，因此只启用打算执行的工具。

## MCP 服务

“MCP 服务”页面编辑最多 16 个 Streamable HTTP 服务，并测试已保存配置。每项包含唯一名称、用途、URL、令牌环境变量名、开关、超时和明确的协议版本。`POST /api/admin/mcp/{name}/test` 发现工具并关闭远端会话；配置通过 `mcp` 功能区保存，实际凭据只在服务端读取，不返回页面。

支持 [2025-11-25](https://modelcontextprotocol.io/specification/2025-11-25/basic/transports)（初始化、初始化通知、可选会话头和 DELETE）与 [2026-07-28](https://modelcontextprotocol.io/specification/2026-07-28/basic/transports/streamable-http)（无状态元数据、方法与名称头、验证后的基本类型参数头）。支持 JSON 和有限长度的 SSE 响应；发现最多读取 20 个不同分页、256 个工具，失败变为有界工具错误。不支持 stdio、旧版 HTTP+SSE、OAuth、prompts/resources、sampling 和交互输入请求。

模型先看到已启用服务的用途目录，调用 `mcp_list_tools` 和 `mcp_enable_tools` 后，被选中的参数 schema 才会进入后续请求。调用名使用 `mcp_<服务名>_<目录序号>`，避免远端名称无效或冲突。选择状态和发现缓存仅属于本轮回复，结束时关闭远端会话。调用逐个执行且不自动重试；结果在长度限制内保留 `content`、`structuredContent` 和 `isError`。MCP 和 Lua HTTP 也进入请求追踪，对应 `source=mcp` 和 `source=tool`。
