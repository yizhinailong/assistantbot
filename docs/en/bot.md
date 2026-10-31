# Bot guide

English · [简体中文](../zh-CN/bot.md)

[Documentation index](README.md) · [Project home](../../README.md)

This page describes how assistantbot is put together, how to configure it, how it behaves in a chat, and how to try it and test it.

## Overview

The app combines six drogon plugins, all registered in `config.json`:

| Plugin | Source | Role |
| --- | --- | --- |
| `drogon_onebot::OneBotClient` | [drogon-onebot](https://github.com/yizhinailong/drogon-onebot) | Registers the event route that NapCat posts to, and calls NapCat's HTTP API to send messages |
| `drogon_openai::OpenAIClient` | [drogon-openai](https://github.com/yizhinailong/drogon-openai) | Calls the chat completions API of an OpenAI-compatible model |
| `storage::Database` | `src/storage/` in this repository | Opens the bot's SQLite database and brings its schema up to date |
| `controllers::AssistantBot` | `src/controllers/`, `src/workflow/`, and `src/common/` in this repository | Decides which messages to answer, keeps the conversations, and connects the other three |
| `admin::Auth` | `src/admin/` | Authenticates administrative HTTP requests and WebSocket upgrades |
| `admin::Runtime` | `src/admin/` | Queries bot and NapCat status, daily usage, and process samples |

A message travels like this:

```
NapCat ──POST /onebot/event──▶ OneBotClient ──204──▶ (NapCat is done)
                                   │
                                   ▼ message handler of AssistantBot, in the background
                         filter, commands, queue per user
                                   │
                                   ▼ system prompt + history + message
                         OpenAIClient: chat completions
                                   │
                                   ▼ reply without Markdown, split if long
NapCat ◀──send_private_msg──── OneBotClient
```

The event route answers 204 before the bot runs, so a slow model never holds up NapCat. `src/main.cpp` aborts at startup when `config.json` does not load `controllers::AssistantBot`, because drogon would only log the missing name and run without it.

The [admin pages](admin.md) can pause new messages before commands and queuing. In-flight work continues; restarting resumes normal operation. Each completed model request appends its outcome and reported token counts to SQLite for the overview's daily UTC totals. Schema version 2 adds `model_usage`, version 3 adds `chat_messages`, and version 4 adds cache hits, preserving existing data. The admin pages also provide live sessions, hot reply settings, usage charts, and filtered logs.

## Configuration

`config.json` registers all six plugins. It is tracked and holds nothing private, so edit it in place; the values that differ per person come from environment variables:

| Environment variable | Read by | Value |
| --- | --- | --- |
| `ONEBOT_ACCESS_TOKEN` | `OneBotClient` (`access_token_env`) | Token of NapCat's HTTP server; sent as `Authorization: Bearer` |
| `ONEBOT_SECRET` | `OneBotClient` (`secret_env`) | Token of NapCat's HTTP client; checks the signature of each event |
| `DEEPSEEK_API_KEY` | `OpenAIClient` (`api_key_env`) | API key of the model provider |
| `ASSISTANTBOT_ADMIN_TOKEN` | `admin::Auth` (`admin_token_env`) | Secret used to sign in to the admin pages |

Secrets are read only from environment variables; `config.json` holds only their names. A plugin whose named variable is unset or empty stops the app at startup.

The two library plugins are described in their own guides: [drogon-onebot](https://github.com/yizhinailong/drogon-onebot/blob/master/docs/en/drogon-plugin.md) and [drogon-openai](https://github.com/yizhinailong/drogon-openai/blob/master/docs/en/drogon-plugin.md). For assistantbot:

- `OneBotClient` needs an `event_path`, or no message reaches the bot.
- `OpenAIClient`'s `base_url` and `model` choose the default endpoint and model. A bot setting can override the model name for each request. `config.json` uses DeepSeek; for OpenAI, use `https://api.openai.com/v1` and an OpenAI model name.

`storage::Database` keeps the bot's data in a SQLite database:

```json
{
    "name": "storage::Database",
    "config": {
        "data_dir": "data"
    }
}
```

| Key | Required | Default | Description |
| --- | --- | --- | --- |
| `data_dir` | No | `data` | Directory of the database file `assistantbot.db`, relative to the working directory. Both are created when missing. |

- `data/` is ignored by Git. Back it up to keep what the bot has saved.
- At startup the plugin brings the database's schema up to date. A database written by a newer version of the bot, or a file that SQLite cannot open, stops the app with a message that starts with `storage::Database:`.

`controllers::AssistantBot` must come after the other three and list them under `dependencies`, so that drogon initializes them first:

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

| Key | Required | Default | Description |
| --- | --- | --- | --- |
| `system_prompt` | No | Asks for plain-text replies in the user's language | First message of every model request. An empty string sends none. |
| `history_turns` | No | 10 | Most past turns (a message and its reply) per user sent back to the model. 0 keeps no history. |
| `idle_reset_minutes` | No | 30 | Silence, in minutes, after which a user's history is cleared. |
| `request_timeout` | No | 90 | Seconds to wait for the model. |
| `max_tokens` | No | Unset | Upper bound on generated tokens; omitted or null leaves it to the provider. |
| `max_reply_chars` | No | 1500 | Most characters in one QQ message; a longer reply is sent as several messages. |
| `model` | No | Empty | Per-request model name; empty uses the OpenAI plugin default. |

An invalid value stops the app at startup with a message that names the key, such as `controllers::AssistantBot: history_turns must be an integer of at least 0`.

The same keys can also be saved in the database, under the setting `bot`, and can be edited in the admin pages at `/#/settings`. A saved key overrides `config.json`; the keys that are not saved keep their `config.json` value.

- Each reply reads the settings when it starts, so a saved change applies from the next reply on. SessionStore also reads current history and idle limits per operation. `{}` restores config.json; `max_tokens: null` removes a limit.
- An invalid saved value stops the app at startup, with the key's message followed by `(in the settings saved in the database)`.

## Behavior in a chat

**Who gets an answer.** By default the bot answers private messages from every friend of its QQ account (`sub_type` `friend`). The [admin pages](admin.md) can restrict replies to an allowlist and blacklist individual senders. The blacklist takes priority in both modes, including for `/help`, `/reset`, and messages without text. Excluded private friend messages are logged and stored with their ignore reason, but never queued or sent to the model; their commands do not change history. Policy changes apply to new messages, while already accepted work continues. Mode and lists persist in SQLite, with immutable snapshots used for each access check. Group messages and temporary sessions started from a group are always ignored.

**Logging.** Every message the bot receives is logged at INFO level before the bot decides whether to answer it, so group messages and temporary sessions show up too. Every message the bot sends is logged once it has been sent; a reply split into several messages gives one line per message:

```
(10000) 接收 <- 私聊 张三(20001) 你好[image]
(10000) 发送 -> 私聊 张三(20001) 你好！图片我看不到，只能读文字。
(10000) 接收 <- 群聊 (30001) 群名片(20001) @10000 在吗
```

The number at the start is the QQ account the bot is logged in as. Then comes the other account, as its name and QQ number, preceded by the group number in a group message. The name is the sender's group card when they set one, otherwise their nickname; when both are empty, only the QQ number in parentheses is left. Text segments appear as they are, `at` segments as `@<QQ number>`, and other segments as `[<type>]`, such as `[image]` or `[face]`. drogon puts the time, the thread ID, and the level in front of each line. A message that fails to send is logged at ERROR level instead.

**Commands.** A message that is exactly a command is answered by the bot itself, without the model:

| Message | Effect |
| --- | --- |
| `/help` | Replies with a short help text |
| `/reset` | Clears the sender's history and queued messages, then confirms |

**Messages without text.** The bot reads only the text segments of a message. A message with no text, such as a lone image or sticker, gets a notice that only text is supported. In a message that mixes text and images, only the text is used.

**Conversation history.** Each user has their own history of turns, kept in memory. The model sees the system prompt, the last `history_turns` turns, and the new message. A history is cleared by `/reset`, after `idle_reset_minutes` of silence, and when the app restarts.

**Chat records.** Each incoming private friend message and outgoing part is appended separately to SQLite, including ignored messages, commands, model failure notices, and failed delivery attempts. The [sessions page](admin.md) paginates them and receives live updates. Reset clears model context and pending messages while preserving stored records; in-flight replies still finish without restoring a reset turn. Stored records do not restore context after restart.

**Messages sent while the bot is answering.** One user never has two model requests at once. Messages that arrive while a reply is being generated are queued, then sent to the model together, joined by line breaks, in one request after the current reply. Several short messages typed in a row therefore get one answer instead of several.

**Replies.** QQ shows Markdown literally, so the bot removes heading markers, `**`, backticks, and code fence lines from the reply (the lines inside a fence stay). A reply longer than `max_reply_chars` characters is split, at line breaks where possible. When the model request fails, times out, or returns no text, the bot sends an apology, logs the reason at ERROR level, and adds no model turn. The incoming message and attempted failure notice remain in chat records.

The fixed texts are constants. `FAILURE_REPLY` is in `src/workflow/private_chat.hpp`, and the others are in `src/controllers/onebot/assistant_bot.hpp`, next to the commands:

| Constant | Text |
| --- | --- |
| `HELP_REPLY` | 直接发消息就能和我聊天。<br>/reset 清空对话历史<br>/help 显示这条帮助 |
| `RESET_REPLY` | 对话历史已清空。 |
| `TEXT_ONLY_REPLY` | 目前只支持文字消息。 |
| `FAILURE_REPLY` | 抱歉，暂时无法回复，请稍后再试。 |

## Trying it without NapCat

`scripts/chat.py` plays both of NapCat's roles in a terminal:

- It serves the OneBot HTTP API on `127.0.0.1:3000`, the `base_url` in `config.json`, and prints every message that the bot sends. When `ONEBOT_ACCESS_TOKEN` is set, it answers 403 to a request whose `Authorization` header does not carry it.
- It posts every line you type to `http://127.0.0.1:8080/onebot/event` as a private message from a friend, signed with `ONEBOT_SECRET`.

It also serves login, status, and version actions so the runtime overview identifies the simulator and the `--self-id` account.

Start the bot with `just run` and the environment variables above, then, in another terminal with the same `ONEBOT_ACCESS_TOKEN` and `ONEBOT_SECRET`, run `python3 scripts/chat.py`. The messages come from QQ number 10001; `--user-id`, `--event-url`, and `--api-port` change the defaults. End the chat with Ctrl-D.

## Connecting NapCat

NapCat's OneBot configuration needs two network endpoints. The [drogon-onebot guide](https://github.com/yizhinailong/drogon-onebot/blob/master/docs/en/drogon-plugin.md) describes them in detail; for this app with the repository's `config.json`:

- An HTTP server on port 3000, whose `token` is the value of `ONEBOT_ACCESS_TOKEN`. The bot calls actions on it.
- An HTTP client whose `url` is `http://127.0.0.1:8080/onebot/event`, whose `token` is the value of `ONEBOT_SECRET`, whose `messagePostFormat` is `array`, and with `reportSelfMessage` off.

The bot listens only on `127.0.0.1`. When NapCat runs elsewhere, for example in a container, change the listener address in `config.json` so that NapCat can reach it, and keep `secret_env` set so that the event route rejects unsigned posts.

## Threads

`number_of_threads` in `config.json` sets drogon's IO threads; `config.json` uses one. The bot's handlers are coroutines, so one thread serves many chats while they wait for the model. The per-user state is guarded by a mutex, which is never held across `co_await`, so more threads are safe too.

The SQLite database has one connection, on a thread of its own, so its statements run one at a time.

## Known limitations

- Only private chats are answered; group chats are not supported.
- Only text is understood and sent. Images and other media are not passed to the model, and the model cannot call tools.
- Model context lives in memory and is lost when the app restarts; stored chat records remain available.
- Access defaults to all friends and can be restricted in the admin pages; apart from one request at a time per user, there is no rate limit.

## Tests

The tests use drogon's own test framework, `drogon/drogon_test.h`. Each `tests/test_*.cpp` builds into one program that links the same `assistantbot-core` OBJECT library as the app and is registered with CTest.

- Run all tests: `just test`, which is equivalent to `ctest --preset linux-debug`.
- Run one program from the repository root: `build/linux-debug/tests/test_private_chat`.
- Run one test case: `build/linux-debug/tests/test_private_chat -r MergesMessagesSentWhileBusy`. Add `-s` to print passing checks, or `-l` to list the test cases.

The app-backed tests load `tests/config.json`, never the app's `config.json`, and set the environment variables that it names. It listens on `127.0.0.1:18082` and points both library plugins at fakes served by the same app. Its database is in `/tmp/assistantbot-test/data`, which every app-backed program empties first; the tests that open databases of their own use fresh directories under the system's temporary directory. All tests run offline.

| Program | What it covers |
| --- | --- |
| `test_config` | Reads the bot's config keys: defaults, every key set, and wrong types and ranges. Also checks that the bot entries of `tests/config.json` and `config.json` parse and list the three dependencies. |
| `test_text` | The log lines of received and sent messages, text extraction from message segments, Markdown removal, and splitting long replies by UTF-8 characters at line breaks. |
| `test_session_store` | Queuing and merging messages while a reply is in flight, the history limit, clearing after silence, `/reset` dropping a reply that was in flight, and releasing a user after a failure. |
| `test_private_chat` | The bot end to end with a fake NapCat API and a fake model: a reply, history sent back and trimmed, `/reset`, `/help`, a message without text, a failing model, merging messages sent while busy, Markdown removal, long replies split, and ignoring group messages and temporary sessions. |
| `test_health` | `GET /api/admin/health` answers `{"status": "ok"}`. |
| `test_storage` | The database plugin of `tests/config.json` and its config errors; creating the directory and the file, a path with a space, a file that SQLite cannot open, rejecting a newer schema, saving, replacing, and rereading settings, and preserving the caller's event loop on successful and failed queries. |
| `test_bot_settings` | Overlaying saved settings on the config: defaults, invalid saved values, saving and applying an update, a reply keeping its snapshot, restoring the config with `{}`, rejecting invalid updates, and rejecting a second save while one is in flight. |
| `test_access_control` | Default policy, blacklist priority, immutable snapshots, reopening the database, invalid saved policies, failed writes, and overlapping updates. |
| `test_admin_access` | Authenticated mode and list changes, input validation, QQ precision, friend annotations and upstream failure, save failures, and HTTP 409 conflicts. |

`test_private_chat` also verifies that excluded senders receive no command, media, or model replies, blocked `/reset` preserves history, list changes affect the next message, and pause still blocks allowlisted friends.

The administration tests also cover session discovery and reset, setting validation and HTTP 409 conflicts, UTC usage grouping and cache reports, log bounds and redaction, WebSocket friend filtering, log replay, origin checks, and logout revocation. `test_messages` verifies stable pagination during appends and database reopening.
