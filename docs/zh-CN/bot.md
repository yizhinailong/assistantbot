# bot 使用说明

[English](../en/bot.md) · 简体中文

[文档索引](README.md) · [项目首页](../../README.zh-CN.md)

本文介绍 assistantbot 的组成、配置方法、在聊天中的行为，以及如何试用和测试。

## 概览

应用由六个 drogon 插件组成，都在 `config.json` 中注册：

| 插件 | 来源 | 作用 |
| --- | --- | --- |
| `drogon_onebot::OneBotClient` | [drogon-onebot](https://github.com/yizhinailong/drogon-onebot) | 注册 NapCat 上报事件的路由，并调用 NapCat 的 HTTP API 发送消息 |
| `drogon_openai::OpenAIClient` | [drogon-openai](https://github.com/yizhinailong/drogon-openai) | 调用兼容 OpenAI 格式的模型的 chat completions 接口 |
| `storage::Database` | 本仓库的 `src/storage/` | 打开 bot 的 SQLite 数据库，并把表结构升级到最新 |
| `controllers::AssistantBot` | 本仓库的 `src/controllers/`、`src/workflow/` 和 `src/common/` | 决定回复哪些消息，保存对话，并把另外三个插件连起来 |
| `admin::Auth` | 本仓库的 `src/admin/` | 认证后台 HTTP 请求和 WebSocket 升级 |
| `admin::Runtime` | 本仓库的 `src/admin/` | 查询 bot、NapCat 状态、当天用量和进程样本 |

一条消息的处理过程：

```
NapCat ──POST /onebot/event──▶ OneBotClient ──204──▶ （NapCat 这边结束）
                                   │
                                   ▼ AssistantBot 的消息回调，在后台运行
                         过滤、命令、按用户排队
                                   │
                                   ▼ system prompt + 历史 + 新消息
                         OpenAIClient：chat completions
                                   │
                                   ▼ 去掉 Markdown，过长时分条
NapCat ◀──send_private_msg──── OneBotClient
```

事件路由在 bot 运行之前就回复 204，所以模型再慢也不会拖住 NapCat。`config.json` 没有加载 `controllers::AssistantBot` 时，`src/main.cpp` 会在启动时直接中止，因为 drogon 只会记录一条日志，然后在没有这个插件的情况下继续运行。

[管理后台](admin.md) 可以在命令处理和入队前暂停新消息，已经开始的工作继续完成，重启后恢复正常运行。每次模型请求完成后，向 SQLite 逐条追加结果和返回的令牌数，用于首页按 UTC 统计当天用量。Schema 版本 2 新增 `model_usage`，版本 3 新增 `chat_messages`，版本 4 增加缓存命中数，保留原有数据。后台还提供实时会话、回复设置热更新、用量图表和日志过滤。

## 配置

`config.json` 注册了全部六个插件。它已提交到仓库，其中没有任何私密信息，直接修改即可；因人而异的值都从环境变量读取：

| 环境变量 | 读取它的插件 | 值 |
| --- | --- | --- |
| `ONEBOT_ACCESS_TOKEN` | `OneBotClient`（`access_token_env`） | NapCat HTTP 服务器的 token，以 `Authorization: Bearer` 发送 |
| `ONEBOT_SECRET` | `OneBotClient`（`secret_env`） | NapCat HTTP 客户端的 token，用于校验每个事件的签名 |
| `DEEPSEEK_API_KEY` | `OpenAIClient`（`api_key_env`） | 模型服务商的 API key |
| `ASSISTANTBOT_ADMIN_TOKEN` | `admin::Auth`（`admin_token_env`） | 管理后台登录密钥 |

密钥只从环境变量读取，`config.json` 里只写变量名。插件指定的变量未设置或为空时，应用会在启动时停止。

两个库插件的配置见各自的使用说明：[drogon-onebot](https://github.com/yizhinailong/drogon-onebot/blob/master/docs/zh-CN/drogon-plugin.md) 和 [drogon-openai](https://github.com/yizhinailong/drogon-openai/blob/master/docs/zh-CN/drogon-plugin.md)。对 assistantbot 来说：

- `OneBotClient` 必须设置 `event_path`，否则没有消息能到达 bot。
- `OpenAIClient` 的 `base_url` 和 `model` 决定默认服务地址和模型；bot 设置可以覆盖每次请求的模型名。`config.json` 用的是 DeepSeek；对接 OpenAI 时，改为 `https://api.openai.com/v1` 和 OpenAI 的模型名。

`storage::Database` 把 bot 的数据保存在 SQLite 数据库中：

```json
{
    "name": "storage::Database",
    "config": {
        "data_dir": "data"
    }
}
```

| 配置项 | 必填 | 默认值 | 说明 |
| --- | --- | --- | --- |
| `data_dir` | 否 | `data` | 数据库文件 `assistantbot.db` 所在的目录，相对于工作目录。目录和文件不存在时会自动创建。 |

- Git 会忽略 `data/`。要保留 bot 保存的内容，请备份这个目录。
- 启动时插件会把数据库的表结构升级到最新。如果数据库来自更新版本的 bot，或者 SQLite 无法打开这个文件，应用会停止，错误信息以 `storage::Database:` 开头。

`controllers::AssistantBot` 必须写在另外三个插件之后，并在 `dependencies` 中列出它们，这样 drogon 会先初始化它们：

```json
{
    "name": "controllers::AssistantBot",
    "dependencies": ["drogon_onebot::OneBotClient", "drogon_openai::OpenAIClient", "storage::Database"],
    "config": {
        "history_turns": 10,
        "idle_reset_minutes": 30,
        "request_timeout": 90,
        "max_reply_chars": 1500,
        "model": ""
    }
}
```

| 配置项 | 必填 | 默认值 | 说明 |
| --- | --- | --- | --- |
| `system_prompt` | 否 | 要求用用户的语言、以纯文本回复 | 每次模型请求的第一条消息。空字符串表示不发送。 |
| `history_turns` | 否 | 10 | 每个用户最多带回给模型的历史轮数（一条消息及其回复为一轮）。0 表示不保留历史。 |
| `idle_reset_minutes` | 否 | 30 | 用户沉默超过这么多分钟后，清空其历史。 |
| `request_timeout` | 否 | 90 | 等待模型回复的秒数。 |
| `max_tokens` | 否 | 不设置 | 生成 token 数的上限；省略或设为 null 时由服务商决定。 |
| `max_reply_chars` | 否 | 1500 | 一条 QQ 消息的最大字符数；更长的回复会分成多条发送。 |
| `model` | 否 | 空字符串 | 每次请求的模型名；留空使用 OpenAI 插件的默认值。 |

取值无效时，应用会在启动时停止，错误信息会指出是哪个配置项，例如 `controllers::AssistantBot: history_turns must be an integer of at least 0`。

这些配置项也可以保存在数据库中（设置名为 `bot`），由管理后台 `/#/settings` 修改。保存了的配置项会覆盖 `config.json`，没有保存的仍使用 `config.json` 中的值。

- 每次回复开始时读取一次设置，所以保存的改动从下一次回复起生效。SessionStore 每次操作也读取当前历史和空闲限制。`{}` 恢复 config.json，`max_tokens: null` 清除令牌限制。
- 保存的值无效时，应用会在启动时停止，错误信息是该配置项的错误加上 `(in the settings saved in the database)`。

## 聊天中的行为

**回复谁。** 默认回复其 QQ 账号所有好友（`sub_type` 为 `friend`）发来的私聊消息。[管理后台](admin.md) 可以改成只回复允许名单内的好友，也可以把指定 QQ 号加入黑名单。黑名单在两种模式下都优先，对 `/help`、`/reset` 和无文字消息同样生效。被拦截的私聊好友消息记日志并保存忽略原因，不入队、不请求模型，命令也不会修改历史。修改策略用于新消息，已经接受的工作继续处理。模式和名单保存在 SQLite，每次检查都读取不可变快照。群消息和从群里发起的临时会话始终忽略。

**日志。** bot 收到的每条消息都会先以 INFO 级别记一行日志，再决定是否回复，所以群消息和临时会话也会出现在日志里。bot 发出的每条消息在发送成功后也记一行；分成多条发送的回复，每条各记一行：

```
(10000) 接收 <- 私聊 张三(20001) 你好[image]
(10000) 发送 -> 私聊 张三(20001) 你好！图片我看不到，只能读文字。
(10000) 接收 <- 群聊 (30001) 群名片(20001) @10000 在吗
```

开头的号码是 bot 登录的 QQ 号，后面是对方的名字和 QQ 号，群聊时前面再加上群号。名字优先用发送者的群名片，没有设置时用昵称；两者都为空时只剩括号里的 QQ 号。文字段原样显示，`at` 段显示为 `@QQ号`，其他消息段显示为 `[类型]`，例如 `[image]`、`[face]`。drogon 会在每行前面加上时间、线程号和级别。发送失败的消息改为以 ERROR 级别记录。

**命令。** 内容恰好是命令的消息由 bot 直接回复，不经过模型：

| 消息 | 效果 |
| --- | --- |
| `/help` | 回复一段简短的帮助 |
| `/reset` | 清空发送者的历史和排队中的消息，然后回复确认 |

**没有文字的消息。** bot 只读取消息里的文字段。没有文字的消息（例如只有一张图片或一个表情）会收到"只支持文字"的提示。文字和图片混发时，只使用其中的文字。

**对话历史。** 每个用户有一份自己的历史，保存在内存里。模型看到的是 system prompt、最近 `history_turns` 轮历史和新消息。`/reset`、沉默超过 `idle_reset_minutes` 分钟、应用重启，都会清空历史。

**聊天记录。** 每条收到的私聊好友消息和发出的分段都独立追加到 SQLite，包括被忽略的消息、命令、模型失败提示和发送失败的尝试。[会话页面](admin.md) 分页读取并接收实时更新。重置清空模型上下文和待处理消息，保留聊天记录；正在生成的回复仍会完成，但不会恢复已重置的轮次。重启后不从聊天记录恢复上下文。

**回复期间发来的消息。** 同一个用户同一时间最多只有一个模型请求。正在生成回复时又发来的消息会先排队，等当前回复发完后，用换行连起来，合成一个请求发给模型。所以连着发的几条短消息只会得到一次回复，而不是好几次。

**回复。** QQ 会把 Markdown 原样显示，所以 bot 会去掉回复中的标题标记、`**`、反引号和代码块的围栏行（围栏之间的内容保留）。超过 `max_reply_chars` 个字符的回复会分成多条，尽量在换行处断开。模型请求失败、超时或没有返回文字时，bot 会回一句道歉，以 ERROR 级别记录原因，不把这一轮放入模型上下文；收到的消息和发送失败提示的尝试仍保存在聊天记录中。

固定的回复文字都是常量。`FAILURE_REPLY` 在 `src/workflow/private_chat.hpp` 中，其余的和命令一起在 `src/controllers/onebot/assistant_bot.hpp` 中：

| 常量 | 文字 |
| --- | --- |
| `HELP_REPLY` | 直接发消息就能和我聊天。<br>/reset 清空对话历史<br>/help 显示这条帮助 |
| `RESET_REPLY` | 对话历史已清空。 |
| `TEXT_ONLY_REPLY` | 目前只支持文字消息。 |
| `FAILURE_REPLY` | 抱歉，暂时无法回复，请稍后再试。 |

## 不用 NapCat 试用

`scripts/chat.py` 在终端里同时扮演 NapCat 的两个角色：

- 在 `127.0.0.1:3000`（`config.json` 中的 `base_url`）提供 OneBot HTTP 接口，打印 bot 发出的每条消息。设置了 `ONEBOT_ACCESS_TOKEN` 时，请求的 `Authorization` 头不带这个 token 就回复 403。
- 把你输入的每一行，作为好友发来的私聊消息，用 `ONEBOT_SECRET` 签名后发到 `http://127.0.0.1:8080/onebot/event`。

它也提供账号、在线状态和版本动作，运行状态首页会显示模拟器及 `--self-id` 指定的账号。

先设置上文的环境变量，用 `just run` 启动 bot；再在另一个终端里设置同样的 `ONEBOT_ACCESS_TOKEN` 和 `ONEBOT_SECRET`，运行 `python3 scripts/chat.py`。消息默认来自 QQ 号 10001；`--user-id`、`--event-url` 和 `--api-port` 可以修改默认值。按 Ctrl-D 结束。

## 连接 NapCat

NapCat 的 OneBot 配置需要两个网络端点，详见 [drogon-onebot 的使用说明](https://github.com/yizhinailong/drogon-onebot/blob/master/docs/zh-CN/drogon-plugin.md)。对使用仓库中 `config.json` 的本应用：

- HTTP 服务器：端口 3000，`token` 为 `ONEBOT_ACCESS_TOKEN` 的值。bot 通过它调用动作。
- HTTP 客户端：`url` 为 `http://127.0.0.1:8080/onebot/event`，`token` 为 `ONEBOT_SECRET` 的值，`messagePostFormat` 为 `array`，关闭 `reportSelfMessage`。

bot 只监听 `127.0.0.1`。NapCat 运行在别处（例如容器里）时，修改 `config.json` 中的监听地址，让 NapCat 能访问到 bot，并保留 `secret_env`，让事件路由拒绝没有签名的请求。

## 线程

`config.json` 中的 `number_of_threads` 设置 drogon 的 IO 线程数，`config.json` 用 1 个。bot 的回调是协程，等待模型时不占用线程，所以一个线程就能同时服务很多个对话。每个用户的状态由一个互斥锁保护，持锁期间从不 `co_await`，所以更多线程也是安全的。

SQLite 数据库只有一个连接，运行在单独的线程上，所以语句逐条执行。

## 已知限制

- 只回复私聊，不支持群聊。
- 只理解和发送文字。图片等媒体不会传给模型，模型也不能调用工具。
- 模型上下文保存在内存里，应用重启后丢失；聊天记录保留在数据库中。
- 默认所有好友都能使用模型，可以在管理后台限制；除了每个用户同时只有一个请求之外，没有其他限流。

## 测试

测试使用 drogon 自带的测试框架 `drogon/drogon_test.h`。每个 `tests/test_*.cpp` 编译成一个程序，和应用链接同一个 OBJECT 库 `assistantbot-core`，并注册到 CTest。

- 运行全部测试：`just test`，相当于 `ctest --preset linux-debug`。
- 在仓库根目录运行一个程序：`build/linux-debug/tests/test_private_chat`。
- 运行一个测试用例：`build/linux-debug/tests/test_private_chat -r MergesMessagesSentWhileBusy`。加 `-s` 输出通过的检查，加 `-l` 列出测试用例。

依赖 app 的测试加载 `tests/config.json`，从不加载应用的 `config.json`，并设置它指定的环境变量。它监听 `127.0.0.1:18082`，让两个库插件都指向同一个 app 提供的假接口。它的数据库位于 `/tmp/assistantbot-test/data`，每个依赖 app 的程序启动前都会先清空这个目录；自己打开数据库的测试则使用系统临时目录下的新目录。所有测试都离线运行。

| 程序 | 覆盖内容 |
| --- | --- |
| `test_config` | 读取 bot 的配置项：默认值、设置了全部配置项，以及类型和取值范围错误。还检查 `tests/config.json` 和 `config.json` 中的 bot 配置能通过校验，并列出了三个依赖。 |
| `test_text` | 收发消息的日志行、从消息段提取文字、去掉 Markdown，以及按 UTF-8 字符数在换行处拆分长回复。 |
| `test_session_store` | 回复期间消息的排队与合并、历史轮数上限、沉默后清空、`/reset` 丢弃正在生成的回复，以及失败后释放用户。 |
| `test_private_chat` | 用假的 NapCat 接口和假的模型端到端测试 bot：正常回复、带回并裁剪历史、`/reset`、`/help`、没有文字的消息、模型失败、合并忙碌时发来的消息、去掉 Markdown、拆分长回复，以及忽略群消息和临时会话。 |
| `test_health` | `GET /api/admin/health` 返回 `{"status": "ok"}`。 |
| `test_storage` | `tests/config.json` 中的数据库插件及其配置错误；创建目录和文件、带空格的路径、SQLite 无法打开的文件、拒绝更新版本的表结构，保存、替换和重新读取设置，以及查询成功和失败后都回到调用方的事件循环。 |
| `test_bot_settings` | 在配置上叠加保存的设置：默认值、无效的保存值、保存并应用更新、回复保留自己的快照、用 `{}` 恢复配置、拒绝无效的更新，以及在一次保存未完成时拒绝第二次保存。 |
| `test_access_control` | 默认策略、黑名单优先级、不可变快照、重新打开数据库、无效保存策略、保存失败和同时修改。 |
| `test_admin_access` | 认证后的模式和名单修改、参数校验、QQ 号精度、好友标记和上游失败、保存失败，以及 HTTP 409 冲突。 |

`test_private_chat` 还验证被拦截者不会收到命令、媒体提示或模型回复，被拦截的 `/reset` 保留历史，名单修改对下一条消息生效，暂停也会拦截允许名单内的好友。

后台测试还覆盖会话发现与重置、设置校验和 HTTP 409 冲突、UTC 用量分组与缓存报告、日志上限与密钥遮蔽、WebSocket 好友筛选、日志补取、Origin 校验和退出登录后的连接撤销。`test_messages` 验证追加期间的稳定分页和数据库重新打开。
