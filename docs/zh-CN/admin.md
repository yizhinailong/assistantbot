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

每条收到的私聊好友消息都单独追加到 SQLite，包括命令、媒体占位文本，以及被访问策略或暂停拦截的消息。发出的每个分段分别记为 `sent` 或 `failed`，失败时保留发送错误；模型失败时发出的提示也会保存。记录使用服务端 UTC Unix 时间戳和十进制字符串 ID。群聊和临时会话只记日志。存储失败会记日志，不阻断回复。

模型上下文仍只保存在内存，不从聊天记录重建。页面重置调用与 `/reset` 相同的 `PrivateChat::Reset`，清空上下文和待处理消息；正在生成的回复仍会完成，但不会写回已清空的上下文。重置保留聊天记录，页面操作不会向 QQ 发送确认消息。

消息接口默认返回最近 50 条，支持 `limit=1..200`，结果包括按时间顺序排列的 `messages`、`has_more`、`next_before` 和 `next_after`。传入不包含边界的 `before` ID 读取更早记录，或用 `after` ID 补取断线期间的记录，两者不能同时使用。页面按 ID 合并去重实时与历史消息，重新连接后补取缺失消息。未知会话的详情和重置返回 404，查询其消息返回空页。

## 回复设置

打开 `/#/settings`，可以修改系统提示词、模型名、历史轮数、空闲重置间隔、请求超时、最大输出令牌和每条消息字数。多行提示词编辑框同时展示内置的 `DEFAULT_SYSTEM_PROMPT`。提示词留空表示不发送系统消息；模型名留空使用 OpenAI 插件默认模型；最大令牌留空表示不发送限制。

`GET /api/admin/bot-config` 返回当前 `values`、已保存的 `overrides`、config.json 中的 `defaults` 和 `default_system_prompt`。`PUT` 整体替换覆盖对象，未包含的键回到 config.json，`{}` 恢复默认。`max_tokens: null` 可以清除配置中的限制。校验失败返回 400，保留 `std::invalid_argument` 的原始信息；同时保存返回 409；存储失败返回 500，保留当前快照。

保存的修改重启后保留，从下一次回复生效。SessionStore 每次操作读取最新的历史和空闲限制；缩小历史轮数后，在下次读取历史、入队或写入时裁掉多余轮次。已经开始的请求继续使用原来的提示词、模型、超时、令牌和回复分段设置。模型覆盖值只填入每次请求的 `ChatCompletionCreateParams::model`，服务商配置仍在 config.json 中；这里不返回或编辑 API key。

## 用量统计

打开 `/#/usage`，查看每日令牌柱状图、数据表、好友排行，以及总计和当天计数。日期输入包含起止两天，统一使用 UTC。失败请求和未返回用量的请求仍计数，令牌总数只累计实际返回的值。

`GET /api/admin/usage?from=&to=` 使用 UTC Unix 秒，包含 `from`、不包含 `to`，默认最近 30 个 UTC 日（含当天），区间必须为正且不超过 366 天。结果包含 `total`、独立于所选区间的 `today`、以零点 `timestamp` 标识的 UTC `days`，以及按十进制字符串 `user_id` 分组的 `users`。全部分组来自同一次 SQLite 读取快照，避免并发追加时各部分不一致。

每次模型请求完成后，保存 QQ 号、模型、成功状态、输入/输出/总令牌和可选的缓存命中数。drogon-openai v0.2.0 提供 `prompt_tokens_details.cached_tokens`，没有该字段时读取 DeepSeek 的 `usage.raw.prompt_cache_hit_tokens`。未知缓存报告保存为 SQL NULL，明确报告零命中也计入 `cached_requests`。Schema 3 新增聊天记录，Schema 4 新增缓存命中字段，保留原有设置和用量。用量保存成功后广播 `usage_updated`，页面重新读取当前所选区间。

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

请求追踪页在内存中保留最近 50 次模型、OneBot 和 MCP 请求，显示方法、地址、状态、毫秒耗时和截断后的请求/响应正文。可以按来源筛选、暂停自动刷新、清空缓冲和下载当前筛选结果的 JSON。管理副本会去掉认证头、已配置的密钥、凭据字段和内联图片数据；正文仍可能包含对话内容。重启后不保留追踪。

`GET /api/admin/http-traces?source=model|onebot|mcp` 读取缓冲，省略来源则读取全部；`DELETE /api/admin/http-traces` 清空；`GET /api/admin/http-traces/download` 下载 JSON，接受相同筛选。`/api/admin/ws` 用 `http_trace_created` 通知新增记录。OneBot 反向 WebSocket 动作不产生 HTTP 追踪。

## 群聊

群聊页通过类型化 OneBot 接口读取已加入的群，所有群默认关闭。每个群可以设置启用、自动回复（关闭时需提及机器人）、群名片/昵称上下文、群命令和附加系统提示词，下一条消息起生效。管理员可以使用 `/on`、`/off`、`/auto on`、`/auto off`、`/prompt 提示词`、`/help`、`/reset`；管理员名单只授予群命令权限。暂停和黑名单仍然有效，私聊允许名单不限制已启用的群。

`GET /api/admin/groups` 合并群列表与配置；`PUT /api/admin/groups/{qq}` 接收 `{values, revision}`。通用配置使用 `GET/PUT /api/admin/features/{section}`，节名为 `groups`、`admins`、`models`、`execution`、`memory`、`tools`、`mcp`。过期版本返回 409。会话接口和订阅中，正数字符串表示好友，负数字符串表示群；消息另含 `sender_id`、`group_id`。重启保留配置和聊天记录，但不从记录恢复模型上下文。
