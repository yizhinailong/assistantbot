# Admin pages

English · [简体中文](../zh-CN/admin.md)

[Documentation index](README.md) · [Project home](../../README.md)

The bot comes with web pages for managing it, in `frontend/`. They are built with Vue 3, Vite, and TypeScript, and call the bot's HTTP API under `/api/admin/`. The runtime overview shows the account, NapCat status, model, sessions, daily model usage, and process resources. Other pages manage access, sessions, reply settings, model roles, memory, tasks, tools, MCP, groups, assets, UTC usage, logs, and HTTP traces.

Sign in with the administrator token before accessing runtime data or controls. Keep the listener on `127.0.0.1`, as `config.json` does; use a reverse proxy with TLS for remote access.

## Authentication

Set `ASSISTANTBOT_ADMIN_TOKEN` to your own non-empty secret before starting the bot. The `admin::Auth` plugin stores only its environment variable name, `admin_token_env`, in `config.json`; an unset or empty variable stops startup. Tokens may contain at most 4096 bytes, are compared through fixed-size SHA-256 digests without an early exit, and are never logged or returned.

Open `/#/login` and enter the token. A successful login sets a host-only `HttpOnly`, `SameSite=Strict` cookie under `/api/admin`. The browser stores the opaque session ID; the pages keep no token in local storage or the URL. Sessions expire after 12 hours, are revoked on logout, and disappear on restart. Each login replaces the browser's previous session; the server retains at most 256 sessions, evicting the oldest when full.

Every request under `/api/admin/`, including WebSocket upgrades, requires a session, except `health`, `auth/login`, and `auth/status`. Unauthenticated requests return JSON with HTTP 401. The pages redirect to login on 401 and return to the original page after signing in. Administrative responses use `Cache-Control: no-store`.

## Runtime overview

- `/#/` shows the compiled project version, startup time, configured model, and the number of users with in-memory session state, including empty or reset sessions. It queries NapCat's typed `GetLoginInfo`, `GetStatus`, and `GetVersionInfo` actions concurrently, with a two-second timeout each. Failed actions produce unknown fields while other data remains available.
- Pause drops new friend messages before commands and queuing, including `/help` and `/reset`. Replies already in progress, including previously queued prompts, finish normally. Resume accepts new messages; dropped messages are not replayed. Ignored private friend messages remain in chat records. The pause flag lives in memory and resets to running on restart.
- Daily usage counts completed model requests and usable replies, plus reported input, output, and total tokens. Records are appended to SQLite, so totals survive restart. “Today” uses UTC midnight boundaries, also returned by the API. Missing token reports are excluded from token totals and counted separately. Commands do not call the model and do not count as requests. A storage error is logged and does not prevent sending a reply.
- Process RSS, peak RSS, and thread count are sampled at startup and every 60 seconds. The latest 1440 samples are kept in memory, in time order, and disappear on restart. Linux reads `/proc/self/status`; Windows uses process APIs. Unavailable measurements are `null`. The memory chart supports pointer selection, arrow keys, and a data table.
- The page refreshes every 60 seconds and has a manual refresh button.

## Access control

Open `/#/access` or choose Access control in the sidebar:

- **All friends** (`all_friends`, the default) replies to every private friend except blacklisted senders. **Allowlist only** (`allowlist`) replies only to listed friends; an empty allowlist blocks every friend.
- The blacklist takes priority in both modes, even when a QQ number is also allowlisted. Excluded messages are logged and saved as ignored chat records, but do not reach commands, text-only notices, the model, or the queue. A blocked `/reset` leaves the conversation history intact. Group messages and temporary sessions are always ignored.
- Save the reply scope explicitly. In the friend table, each button immediately adds or removes that QQ number from the selected list. Search by QQ number, nickname, or remark, and filter by list. Manual QQ entry and list-only records remain available when NapCat cannot supply the friend list.
- Mode and lists are saved together as the `access` row in SQLite's `settings` table. Successful saves apply to new messages and survive restart. Already accepted work continues; previously excluded messages are not replayed. Both lists survive mode switches. A failed save preserves the active policy; simultaneous writes return 409 and can be retried. An invalid saved policy stops startup.
- Friend data comes from typed `GetFriendList` with a two-second timeout. HTTP 502 means that NapCat could not supply the list, while local policy endpoints remain available. The table's reply status describes policy permission; pausing the bot still blocks new messages.

## Sessions and chat records

Open `/#/sessions` to search friends, inspect the latest model turns, read older messages, and reset context. The list includes QQ number, nickname, retained turns, last activity, and whether a reply is in progress. Peers with saved messages remain visible after restart, with zero in-memory turns.

Each incoming private friend or group message is appended to SQLite individually, including commands, media placeholders, and messages ignored by access control or pause. Each outgoing part is recorded as `sent` or `failed`, with a delivery error on failure. Model failure notices are retained too. Records have server UTC Unix timestamps and decimal string IDs. Temporary private-session events remain available in logs only. A storage failure is logged and does not stop replies.

Model context stays in memory and is never rebuilt from stored messages. Reset calls the same `PrivateChat::Reset` as `/reset`: it clears context and pending messages. An in-flight reply still finishes, but its turn cannot return to the cleared context. Reset leaves saved chat records intact and sends no QQ confirmation from the web page.

The message endpoint defaults to the latest 50 records, accepts `limit=1..200`, and returns chronological `messages`, `has_more`, `next_before`, and `next_after`. Use an exclusive `before` ID for older pages or an exclusive `after` ID for reconnect recovery; both cannot be supplied together. The page deduplicates live and persisted messages by ID and recovers missed messages after reconnecting. Unknown session details and resets return 404; querying an unknown friend's messages returns an empty page.

## Reply settings

Open `/#/settings` to edit the system prompt, model name, history size, idle reset interval, request timeout, maximum output tokens, and message length. The multiline prompt editor also shows the built-in `DEFAULT_SYSTEM_PROMPT`. Empty prompt sends no system message; empty model uses the OpenAI plugin's configured model; empty maximum tokens sends no token limit.

`GET /api/admin/bot-config` returns `values`, saved `overrides`, config.json `defaults`, and `default_system_prompt`. `PUT` replaces the entire override object. Omitted keys inherit config.json; `{}` restores defaults. `max_tokens: null` clears a configured limit. Validation errors return 400 with the original `std::invalid_argument` message; overlapping saves return 409; failed storage returns 500 and keeps the active snapshot.

Saved changes survive restart and apply from the next reply. SessionStore reads current history and idle limits on each operation; reducing history trims retained turns on its next history/queue/write operation. In-flight requests keep their prompt, model, timeout, token limit, and reply splitting settings. The model override fills `ChatCompletionCreateParams::model` per request; provider configuration stays in config.json. API keys are never returned or editable here.

## Usage statistics

Open `/#/usage` for daily token bars, a data table, friend rankings, and total/today counters. Date inputs include both selected days and use UTC. Failed requests and requests without usage reports are counted; token totals only include values actually reported.

`GET /api/admin/usage?from=&to=` accepts UTC Unix seconds, with inclusive `from` and exclusive `to`. The default is the last 30 UTC calendar days including today; ranges must be positive and at most 366 days. It returns `total`, an independent `today` bucket, UTC `days` keyed by midnight `timestamp`, and `users` keyed by decimal string `user_id`. All buckets come from one SQLite read snapshot, so concurrent appends do not mix query states.

Each completed model request stores QQ number, model, success, prompt/completion/total tokens, and optional cache hits. drogon-openai v0.3.0 provides `prompt_tokens_details.cached_tokens`; DeepSeek's `usage.raw.prompt_cache_hit_tokens` is the fallback. Unknown cache reports remain SQL NULL, while reported zero hits count in `cached_requests`. Schema 3 adds chat records; schema 4 adds cache hits, preserving settings and old usage rows. Saved usage broadcasts `usage_updated`, and the page refreshes its selected range.

## Logs

Open `/#/logs` to view the latest 5000 process logs, pause display, enable automatic scrolling, and filter by minimum severity, case-sensitive keyword, or a complete QQ number. Changing filters reads the latest 200 matching records. Resume and reconnect request records after the last examined ID; a `truncated` flag warns when the ring has already evicted part of the gap. The browser also caps displayed entries at 5000.

The administrative copy chains Trantor's configured output and flush callbacks, so console/file logging keeps working. Known administrator, OneBot, and provider secrets are redacted in this copy. Capturing starts before listeners accept requests and ends on shutdown; the ring is in memory only. WebSocket delivery runs in batches of at most 200 records per connection each second, avoiding recursive pushes from the logger callback.

`GET /api/admin/logs` accepts `level`, `keyword` (at most 256 bytes), `qq`, `limit=1..5000` (default 200), and optional exclusive `after`. It returns `entries`, `cursor`, `oldest_id`, `truncated`, the current `log_level`, and canonical `filters`. Without `after`, it returns the latest matching entries; with `after`, it scans forward, advancing across nonmatching records too. Log IDs are process-local decimal strings. `PUT /api/admin/log-level` accepts only `{level: "TRACE"|"DEBUG"|"INFO"|"WARN"|"ERROR"|"FATAL"}` and changes subsequent logging with `trantor::Logger::setLogLevel`; restart restores config.json's level.

## Live protocol

Both WebSockets use the administrative session cookie and send `{"type":"ready"}` after connecting. Expiry, logout, login rotation, and session eviction close associated connections. Browser upgrade origins must match the request host. Clients reconnect with a status check and restore their subscription.

- `/api/admin/ws` initially receives all application events. Send `{"type":"subscribe","user_id":"12345"}` to select a friend, or use `null` to receive all friends. The acknowledgment is `subscribed`. `message_created` includes `user_id` and one persisted `message`; `session_updated` asks clients to refresh session state. Friend filtering applies to these two event types. `usage_updated` (with the request's `user_id`) and `settings_updated` are broadcast to all application subscribers.
- `/api/admin/logs/ws` starts sending logs after `{"type":"subscribe","level":"WARN","keyword":"","qq":"12345","after":"123"}`. Filters and `after` are optional; `after` is a decimal string cursor. Replies and live batches use `type: "logs"` with the same fields as GET logs. Invalid subscriptions return `{type: "error", error: "..."}` and preserve the previous subscription.

## Requirements

| Tool | Version | Notes |
| --- | --- | --- |
| Node.js | `^22.18.0` or `>=24.12.0` | The `engines` range of `frontend/package.json`; CI uses Node.js 24 |
| pnpm | 12.15.0 | Named by `packageManager` in `frontend/package.json`. Install it with `npm install -g pnpm@12.15.0`, or let Corepack pick it |

The bot itself builds and runs without them. Without a build of the pages, the bot answers 404 at `/` and otherwise works as usual.

From the repository root, install the dependencies of the pages once, and again whenever `frontend/pnpm-lock.yaml` changes:

```sh
just frontend-init
```

## Development

Run the bot and Vite's development server in two terminals:

```sh
just run            # the bot on 127.0.0.1:8080, with the environment variables of the bot guide
just frontend-dev   # the pages on http://localhost:5173/
```

Vite serves the pages with hot reload and forwards `/api`, WebSockets included, to `http://127.0.0.1:8080`. When you change the bot's listener, change `server.proxy` in `frontend/vite.config.ts` as well.

## Pages served by the bot

```sh
just frontend-build
just run
```

`just frontend-build` type-checks the pages and writes them to `frontend/dist/`. `config.json` sets `document_root` to `frontend/dist`, which drogon resolves against the working directory, the repository root for `just run`. The bot then serves the pages at `http://127.0.0.1:8080/`.

- The pages use hash routes (`/#/...`), so every page loads from `/` and drogon needs no fallback route.
- `upload_path` is the absolute path `/tmp/assistantbot/uploads`. Drogon creates it on startup, and a relative path would put it inside `frontend/dist/`.

`just publish` builds the pages too, and installs them into `publish/assistantbot/frontend/dist/`, next to the executable. Run the bot from `publish/assistantbot/` with a `config.json` there, and the same `document_root` finds them.

## API

The pages call the bot through `request()` in `frontend/src/api/client.ts`, and keep one typed function per route in `frontend/src/api/admin.ts`.

| Route | Answer |
| --- | --- |
| `GET /api/admin/health` | `{"status": "ok"}` |
| `POST /api/admin/auth/login` | Body `{token}`; `{authenticated: true}` and a session cookie, 400 for invalid input, 401 for an incorrect token |
| `GET /api/admin/auth/status` | `{authenticated: boolean}`, publicly available |
| `POST /api/admin/auth/logout` | Revokes the current session and expires the cookie |
| `GET /api/admin/status` | `version`, `started_at` (Unix seconds), `model`, `session_count`, `paused`, `bot` (`user_id` as a string, `nickname`), `napcat`, and `today_usage` |
| `GET /api/admin/bot-status` | `{paused: boolean}` |
| `POST /api/admin/bot-status` | Body `{paused: boolean}`; updated pause flag, 400 for invalid input |
| `GET /api/admin/process-metrics` | `interval_seconds: 60`, `capacity: 1440`, and chronological `samples` with `timestamp`, `rss_bytes`, `peak_rss_bytes`, and `threads` |
| `GET /api/admin/access` | `{mode: "all_friends" \| "allowlist"}` |
| `PUT /api/admin/access` | Body containing only `{mode}`; saved mode, preserving both lists |
| `GET /api/admin/allowlist`, `GET /api/admin/blacklist` | `{qqs: ["12345", ...]}`, sorted numerically |
| `POST /api/admin/allowlist`, `POST /api/admin/blacklist` | Body containing only `{qq: "12345"}`; idempotently adds the entry and returns the saved `{qqs}` |
| `DELETE /api/admin/allowlist/{qq}`, `DELETE /api/admin/blacklist/{qq}` | Idempotently removes the entry and returns the saved `{qqs}` |
| `GET /api/admin/friends` | `{friends: [{user_id, nickname, remark, in_allowlist, in_blacklist, allowed}, ...]}`; 502 on upstream failure |
| `GET /api/admin/sessions` | `{sessions: [{user_id, nickname, turns, last_active, busy}, ...]}` |
| `GET /api/admin/sessions/{qq}` | Metadata and chronological `history: [{user, assistant}, ...]` |
| `POST /api/admin/sessions/{qq}/reset` | Resets context and queue; returns updated detail |
| `GET /api/admin/sessions/{qq}/messages` | Stable chronological message page; optional `limit`, `before`, or `after` |
| `GET /api/admin/bot-config` | `values`, `overrides`, `defaults`, and `default_system_prompt` |
| `PUT /api/admin/bot-config` | Complete overrides object; `{}` restores config.json defaults |
| `GET /api/admin/usage` | Optional UTC `from`/`to`; `total`, `today`, `days`, and `users` |
| `GET /api/admin/logs` | Filtered buffered entries and recovery cursor |
| `PUT /api/admin/log-level` | Body `{level}`; updated runtime logging level |
| WebSocket `/api/admin/ws`, `/api/admin/logs/ws` | Application events and filtered log batches, as described above |

Access endpoints require authentication. Invalid input returns 400, overlapping saves 409, and storage failures 500, with an `error` field. QQ inputs must be decimal **strings** representing positive signed 64-bit integers (maximum `9223372036854775807`); signs, whitespace, fractions, and JSON numbers are rejected. Leading zeroes are normalized. Outputs also use decimal strings, preserving values beyond JavaScript's exact integer range. Mode updates never implicitly clear lists, and editing one list leaves the other unchanged.

`request()` sends a body as JSON and parses a JSON answer. An empty answer, such as a 204, resolves to `undefined`. A failed request throws `ApiError`:

- `status` is the HTTP status, or 0 when the bot could not be reached.
- `message` comes from an `{"error": "<message>"}` body, the shape that admin routes use for errors, and is otherwise the status line.

## Checks

| Recipe | Runs in `frontend/` |
| --- | --- |
| `just frontend-init` | `pnpm install --frozen-lockfile` |
| `just frontend-dev` | `pnpm dev` |
| `just frontend-build` | `pnpm build`: `vue-tsc` type check and `vite build` |
| `just frontend-check` | `pnpm check`: type check, `oxlint` and ESLint with no warnings allowed, and `prettier --check` on `src/` |
| `just frontend-fix` | `pnpm lint` and `pnpm format`: apply the lint fixes, then format `src/` with Prettier |

Linux CI runs `pnpm check` and the build in a separate job; see [Version tags and Linux CI](ci.md).

## HTTP traces

The HTTP traces page retains the latest 50 model, OneBot, MCP, and Lua HTTP attempts in memory. It shows method, endpoint, status, elapsed milliseconds, and bounded request/response bodies. Filter by source, pause automatic refresh, clear the buffer, or download the current selection as JSON. Authentication headers, configured credentials, credential-shaped fields, and inline image data are removed from the administrative copy. Request bodies can still contain conversation text. No trace survives a restart.

`GET /api/admin/http-traces?source=model|onebot|mcp|tool` reads the buffer; omit `source` for all requests. `DELETE /api/admin/http-traces` clears it. `GET /api/admin/http-traces/download` downloads JSON and accepts the same filter. `/api/admin/ws` announces new entries with `http_trace_created`. Reverse WebSocket OneBot actions produce no HTTP trace.

## Group conversations

The Groups page calls the typed OneBot group list. Groups start disabled. Each group can enable replies, automatic replies (otherwise mention the bot), sender card/nickname context, command handling, and an additional system prompt. Changes apply to the next message. Configured administrators may use `/on`, `/off`, `/auto on`, `/auto off`, `/prompt text`, `/help`, and `/reset`; this list grants only group command permission. Pause and blacklists still apply. Private allowlists do not restrict enabled groups.

`GET /api/admin/groups` merges membership with settings; `PUT /api/admin/groups/{qq}` accepts `{values, revision}`. Feature sections use `GET/PUT /api/admin/features/{section}` with `groups`, `admins`, `models`, `execution`, `memory`, `tools`, or `mcp`. A stale revision returns 409. Conversation APIs and subscriptions use positive decimal friend keys and negative decimal group keys; QQ identifiers remain strings. Persisted messages additionally carry `sender_id` and `group_id`. Restarting retains configuration and history without restoring model context.

## Stickers and character reference

The Assets page lists up to 200 NapCat favourites and edits their descriptions. Adding a favourite requires an existing image path on the **NapCat host**, including when NapCat runs in a container. Removing a favourite changes the QQ account's collection. These calls use NapCat extensions; other OneBot implementations can return 502 with the action error. The API is `GET/POST /api/admin/stickers` and `PUT/DELETE /api/admin/stickers/{res_id}`.

Upload a static PNG, JPEG, or WebP character image of at most 8 MiB. It persists in SQLite and is available as the character reference for image-generation tools. `GET/POST/DELETE /api/admin/character-image` reads, replaces, and removes it. POST takes `{data_url, revision}` and DELETE accepts `?revision=`; a stale revision returns 409. The configured request body limit is 12 MiB to accommodate base64 encoding.

## Model roles and execution

The Models page configures `router`, `decision`, `executor`, `executor_fallback`, `thinking`, `vision`, `image_generation`, `memory`, and `embedding`. All optional roles start disabled. A disabled executor uses the primary endpoint and reply settings. Each enabled role can select its own model, base URL, credential environment-variable name, timeout, retry count, token limit, temperature, and reasoning effort. A custom base URL clears inherited authentication; configure its environment name explicitly. No API exposes the resolved key.

Router analysis and reply decisions run before the executor. Thinking and image generation are callable tools; vision describes accepted incoming HTTP images and supports follow-up questions about those images. Memory and embedding support extraction and recall. The fallback executor runs when the primary executor request fails. Every model request, including auxiliary and failed requests, records its own usage. Image requests have no automatic retry.

Execution limits bound tool rounds (default 6), tool result characters (4000), MCP discovery page size (10), tools enabled per selection (4), and selected tools per reply (12). Feature settings are read once for a reply; updates affect the next reply. Names and schemas offered in a request determine which calls may execute. Repeated call IDs cannot repeat side effects. A turn accepts at most 16 calls per model response.

## People and memory

The Memory page edits conversation aliases, global person profiles, relationships, recent interactions, affinity, and conversation-scoped long-term facts. Pick friend/group and QQ to load a conversation, then edit entries or add a fact. A person's profile is shared by QQ across conversations; aliases and long-term facts remain conversation-specific. Revisions prevent stale edits and deletes. Memory and affinity start disabled. Disabling affinity keeps stored values but omits them from model context and automatic updates.

With memory enabled, the executor receives labelled reference data rather than instructions. The optional memory role processes bounded batches of successful turns after `maintenance_turns` replies (default 10), retaining failed batches for another attempt. It extracts only facts about the identified sender and limits affinity changes to ±5 per batch within [-100, 100]. This implementation does not schedule a daily person consolidation. The optional embedding role stores vectors with endpoint/model identity; recall skips incompatible spaces or dimensions and uses cosine similarity. Without embeddings, recall uses a literal substring match. Stored chat messages never restore short-term model context.

| API | Body / result |
| --- | --- |
| `GET /api/admin/memory-documents/{kind}` | Lists `aliases` or `people` documents |
| `GET/PUT/DELETE /api/admin/memory-documents/{kind}/{key}` | PUT `{value, revision}`; DELETE `?revision=` |
| `GET /api/admin/memories?session=&limit=` | Bounded entries; vectors omitted, dimensions included |
| `POST /api/admin/memories` | `{session, content}` |
| `PUT /api/admin/memories/{id}` | `{session, content, revision}` |
| `DELETE /api/admin/memories/{id}?revision=` | Revision-guarded delete |
| `POST /api/admin/memories/search` | `{session, query}`; ranked recall |

## Scheduled tasks

The Tasks page creates, lists, and cancels persistent reminders. Enter a local date/time; the browser sends UTC Unix seconds. Daily tasks repeat at the same UTC time every 24 hours. Tasks belong to one conversation, with at most 100 pending tasks per conversation. `GET/POST /api/admin/tasks` lists or creates `{session, due_at, content, daily}`; GET optionally filters `?session=`. `POST /api/admin/tasks/{id}/cancel` cancels only pending work and returns 409 after delivery has started.

A one-second timer atomically claims due tasks before running the prompt through the normal chat workflow. It checks pause, private access, sender blacklist, and group enablement again. Failures are retained in the task list. Restart marks interrupted running tasks failed and does not replay their delivery. Pending tasks survive restart; a delayed daily task executes once and advances to the next future UTC deadline. This avoids automatically repeating uncertain sends. Cancelling a running task is intentionally unsupported.

## Built-in and Lua tools

The Tools page enables the tool loop and edits each built-in tool's switch and description. `GET /api/admin/tools/builtin` returns the stable catalog. It includes final reply/no-reply/quote, intermediate replies, thinking and vision, long-term memory, reminders, faces and images, favourite sticker operations, group mentions/mutes, and MCP discovery/selection. Group-only tools are absent from private requests. Tools that require disabled memory or auxiliary model roles are also omitted. Source metadata supplies the current message ID and image count; `ask_images` takes zero-based image indices, and resolves only URLs from that message. Model tools and Lua host functions send and schedule only in their current conversation. Reminder cancellation and memory deletion enforce the same scope. Quoting is limited to the current incoming message. Text and quoted tool replies obey `max_reply_chars`; only the first part retains the quote. These restrictions apply even if model arguments request another recipient.

Lua documents are stored in SQLite with revisions. The editor supports add/edit/delete, JSON import/export, sample arguments, and a test button. Tests mock HTTP, messages, and reminders: they produce an operation list without real effects. Use `GET /api/admin/tools/lua`, `GET/PUT/DELETE /api/admin/tools/lua/{name}`, and `POST /api/admin/tools/lua-test`. PUT takes `{value, revision}`; DELETE uses `?revision=`; test takes `{name, value, arguments}`. A document has `enabled`, `description`, `parameters`, and `script`. Names are ASCII identifiers up to 48 characters, excluding reserved built-in/MCP names. Arguments use a validated JSON Schema subset: objects, arrays, primitives, required fields, enums, numeric/length limits, and additional-properties switches.

```lua
function run(args, ctx)
  local response = http.get("https://example.com/search?q=" .. url.encode(args.query))
  return {status = response.status, body = response.body}
end
```

Every call gets a fresh restricted Lua state. `ctx` contains string `session_id` and `user_id`, and boolean `is_private`. Hosts are `http.request/get/post`, `bot.send_message`, and `bot.schedule(due_at, text, daily)`. HTTP results contain `ok`, `status`, `headers`, and `body` or `error`; HTTP 4xx/5xx are responses, and redirects are not followed. `json.encode/decode/null` and `url.encode` are available. File, OS, package, debug, dynamic loading, and direct coroutine APIs are unavailable. Limits are 256 KiB source, 16 MiB Lua allocation, one million instructions, 100 ms active execution excluding awaited hosts, 32 host operations, 1 MiB HTTP/JSON payloads, 32 JSON levels, and a total JSON conversion budget. Standard pattern matching functions are omitted; `string.find` performs a literal search. HTTP can reach the bot host's network, so enable only tools you intend to run.

## MCP services

The MCP page edits up to 16 Streamable HTTP services and probes their saved configuration. Each service has a unique name, description, URL, token environment name, enabled flag, timeout, and explicit protocol version. `POST /api/admin/mcp/{name}/test` discovers tool schemas and closes the remote session. Saving uses the `mcp` feature section. Credentials are resolved server-side and never returned.

The supported protocols are [2025-11-25](https://modelcontextprotocol.io/specification/2025-11-25/basic/transports) (initialize, initialized notification, optional session header and DELETE) and [2026-07-28](https://modelcontextprotocol.io/specification/2026-07-28/basic/transports/streamable-http) (stateless request metadata, method/name headers, validated primitive parameter headers). JSON and finite SSE responses are supported; discovery follows at most 20 distinct pages and accepts 256 tools. Discovery failures become bounded tool errors. The client does not support stdio, old HTTP+SSE, OAuth, prompts/resources, sampling, or interactive input requests.

The model sees a directory of enabled service descriptions. It calls `mcp_list_tools`, then `mcp_enable_tools`; only selected schemas appear in subsequent model requests. Generated names use `mcp_<service>_<catalog-index>` to avoid invalid or colliding remote identifiers. Selection and the discovery cache last for one reply, then remote sessions close. Calls are sequential and never automatically retried. Remote `content`, `structuredContent`, and `isError` remain available in the bounded result. MCP and Lua HTTP attempts also appear in HTTP traces (`source=mcp` or `source=tool`).
