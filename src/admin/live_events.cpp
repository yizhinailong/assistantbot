/**
 * @file live_events.cpp
 * @brief Implement authenticated event subscriptions and bounded process logs.
 */
#include "admin/live_events.hpp"

#include <algorithm>
#include <array>
#include <atomic>
#include <deque>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <string_view>
#include <utility>

#include <drogon/drogon.h>
#include <json/json.h>

#include "admin/auth.hpp"
#include "admin/http_helpers.hpp"
#include "common/time.hpp"
#include "workflow/access_control.hpp"

namespace admin {

    namespace {

        constexpr std::array<char const*, 6> LEVELS{ "TRACE", "DEBUG", "INFO", "WARN", "ERROR", "FATAL" }; ///< Ordered logger severities.
        constexpr std::size_t LOG_CAPACITY{ 5000 };                                                        ///< Maximum retained process records.

        /**
         * @brief Access Trantor's protected callback hooks to chain its configured sink.
         */
        class LoggerBridge : public trantor::Logger {
        public:
            /**
             * @brief Copy the current output callback without replacing file logging.
             * @return Configured sink.
             */
            static auto Output() -> std::function<void(char const*, std::uint64_t)> { return outputFunc_(); }

            /**
             * @brief Copy the current flush callback.
             * @return Configured flush function.
             */
            static auto Flush() -> std::function<void()> { return flushFunc_(); }
        };

        /**
         * @brief State of an authenticated WebSocket with independent filters and cursor.
         */
        struct Subscriber {
            std::weak_ptr<drogon::WebSocketConnection> connection; ///< Non-owning connection reference.
            std::string session;                                   ///< Cookie used for logout and eviction revocation.
            std::chrono::steady_clock::time_point expires;         ///< Original absolute session deadline.
            bool logs{};                                           ///< Whether log polling is enabled.
            bool subscribed{};                                     ///< Whether a log client has supplied its initial filters.
            std::optional<std::string> user_id;                    ///< Optional private friend subscription.
            LogFilter filter;                                      ///< Per-client log filters.
            std::int64_t cursor{};                                 ///< Last examined log ID.
            std::uint64_t revision{};                              ///< Subscription version used to reject stale timer batches.
        };

        /**
         * @brief One ring-buffer entry with a stable numeric cursor.
         */
        struct LogRecord {
            std::int64_t id{}; ///< Process-local monotonic ID.
            int level{};       ///< Ordered severity.
            Json::Value body;  ///< Public redacted representation.
        };

        /**
         * @brief Shared state whose lifetime is independent of plugin shutdown order.
         */
        struct LiveState {
            std::mutex clients_mutex;                                   ///< Guards subscriptions.
            std::map<drogon::WebSocketConnection*, Subscriber> clients; ///< Connections by identity.
            std::mutex logs_mutex;                                      ///< Guards the log ring and next ID.
            std::deque<LogRecord> records;                              ///< Latest log records in order.
            std::int64_t next_id{ 1 };                                  ///< Next log cursor.
            std::atomic_bool active{};                                  ///< Whether capture is enabled.
            std::vector<std::string> secrets;                           ///< Values redacted only in the admin copy.
            trantor::TimerId timer{ trantor::InvalidTimerId };          ///< Log delivery and expiry timer.
        };

        /**
         * @brief Get process-local stream state.
         * @return Shared registry and log ring.
         */
        auto state() -> LiveState& {
            // The logger callback can outlive application singletons during process teardown.
            static LiveState* const live{ new LiveState{} };
            return *live;
        }

        /**
         * @brief Serialize compact event JSON.
         * @param value Event to send.
         * @return Wire representation.
         */
        auto wire(Json::Value const& value) -> std::string {
            Json::StreamWriterBuilder writer{};
            writer["indentation"] = "";
            return Json::writeString(writer, value);
        }

        /**
         * @brief Find a severity's numeric position.
         * @param name Uppercase level.
         * @return Trantor level index.
         * @throws std::invalid_argument If the name is unknown.
         */
        auto level_index(std::string const& name) -> int {
            auto const found{ std::ranges::find(LEVELS, name) };
            if (found == LEVELS.end()) {
                throw std::invalid_argument{ "level must be TRACE, DEBUG, INFO, WARN, ERROR, or FATAL" };
            }
            return static_cast<int>(found - LEVELS.begin());
        }

        /**
         * @brief Match a QQ number as complete decimal digits in a log line.
         * @param line Log text.
         * @param qq Canonical QQ number; empty matches all.
         * @return Whether an exact numeric token is present.
         */
        auto matches_qq(std::string const& line, std::string const& qq) -> bool {
            if (qq.empty()) {
                return true;
            }
            auto digit{ [](char ch) { return ch >= '0' && ch <= '9'; } };
            for (auto at{ line.find(qq) }; at != std::string::npos; at = line.find(qq, at + 1)) {
                if ((at == 0 || !digit(line[at - 1])) && (at + qq.size() == line.size() || !digit(line[at + qq.size()]))) {
                    return true;
                }
            }
            return false;
        }

        /**
         * @brief Save a redacted log copy without recursively logging from the callback.
         * @param message Formatted log bytes.
         * @param size Byte count.
         */
        auto capture(char const* message, std::uint64_t size) -> void {
            auto& live{ state() };
            if (!live.active.load(std::memory_order_acquire)) {
                return;
            }
            try {
                std::string text{ message, static_cast<std::size_t>(size) };
                int level{ 2 };
                auto level_at{ std::string::npos };
                for (std::size_t i{}; i < LEVELS.size(); ++i) {
                    auto const at{ text.find(std::string{ " " } + LEVELS[i] + " ") };
                    if (at < level_at) {
                        level_at = at;
                        level = static_cast<int>(i);
                    }
                }
                for (auto const& secret : live.secrets) {
                    if (secret.empty()) {
                        continue;
                    }
                    for (auto at{ text.find(secret) }; at != std::string::npos; at = text.find(secret, at + 10)) {
                        text.replace(at, secret.size(), "[redacted]");
                    }
                }
                if (text.size() > 16384) {
                    text.resize(16384);
                    text += " [truncated]";
                }
                std::lock_guard lock{ live.logs_mutex };
                Json::Value body{ Json::objectValue };
                body["id"] = std::to_string(live.next_id);
                body["timestamp"] = Json::Int64{ common::unix_seconds() };
                body["level"] = LEVELS[static_cast<std::size_t>(level)];
                body["message"] = std::move(text);
                live.records.push_back({ live.next_id++, level, std::move(body) });
                while (live.records.size() > LOG_CAPACITY) {
                    live.records.pop_front();
                }
            } catch (...) {
                // Logging must keep working even if the optional admin copy cannot be allocated.
            }
        }

        /**
         * @brief Expire connections and deliver at most one bounded log batch per second.
         */
        auto tick() -> void {
            auto& live{ state() };
            std::vector<std::pair<drogon::WebSocketConnectionPtr, Subscriber>> clients{};
            {
                std::lock_guard lock{ live.clients_mutex };
                for (auto it{ live.clients.begin() }; it != live.clients.end();) {
                    if (auto connection{ it->second.connection.lock() }) {
                        clients.emplace_back(std::move(connection), it->second);
                        ++it;
                    } else {
                        it = live.clients.erase(it);
                    }
                }
            }
            for (auto const& [connection, subscriber] : clients) {
                if (subscriber.expires <= std::chrono::steady_clock::now()) {
                    connection->forceClose();
                    continue;
                }
                if (!subscriber.logs || !subscriber.subscribed) {
                    continue;
                }
                auto batch{ read_logs(subscriber.filter, subscriber.cursor, 200) };
                {
                    std::lock_guard lock{ live.clients_mutex };
                    auto const found{ live.clients.find(connection.get()) };
                    if (found == live.clients.end() || found->second.cursor != subscriber.cursor || found->second.revision != subscriber.revision) {
                        continue;
                    }
                    found->second.cursor = decimal(batch["cursor"].asString());
                }
                if (!batch["entries"].empty() || batch["truncated"].asBool()) {
                    batch["type"] = "logs";
                    connection->send(wire(batch));
                }
            }
        }

    } // namespace

    /**
     * @brief Start log capture while preserving the configured output and flush callbacks.
     * @param secrets Values to redact from the administrative log copy.
     * @note Call once before serving requests; stdout/file output remains unchanged.
     */
    auto start_live_events(std::vector<std::string> secrets) -> void {
        auto& live{ state() };
        live.secrets = std::move(secrets);
        live.active = true;
        auto output{ LoggerBridge::Output() };
        trantor::Logger::setOutputFunction([output = std::move(output)](char const* message, std::uint64_t size) {
            output(message, size);
            capture(message, size);
        },
                                           LoggerBridge::Flush());
        live.timer = drogon::app().getLoop()->runEvery(1.0, tick);
    }

    /**
     * @brief Stop timers, close subscribers, and disable administrative log capture.
     */
    auto stop_live_events() -> void {
        auto& live{ state() };
        live.active = false;
        if (live.timer != trantor::InvalidTimerId) {
            drogon::app().getLoop()->invalidateTimer(live.timer);
            live.timer = trantor::InvalidTimerId;
        }
        std::vector<drogon::WebSocketConnectionPtr> connections{};
        {
            std::lock_guard lock{ live.clients_mutex };
            for (auto const& [key, subscriber] : live.clients) {
                if (auto connection{ subscriber.connection.lock() }) {
                    connections.push_back(std::move(connection));
                }
            }
            live.clients.clear();
        }
        for (auto const& connection : connections) {
            connection->forceClose();
        }
    }

    /**
     * @brief Publish a persisted message, session update, or usage update to matching subscribers.
     * @param event Object with a type and optional decimal string user_id.
     */
    auto publish_event(Json::Value event) -> void {
        auto& live{ state() };
        auto const friend_scoped{ event["type"] == "message_created" || event["type"] == "session_updated" };
        std::vector<drogon::WebSocketConnectionPtr> connections{};
        {
            std::lock_guard lock{ live.clients_mutex };
            for (auto const& [key, subscriber] : live.clients) {
                if (!subscriber.logs && subscriber.expires > std::chrono::steady_clock::now() && (!friend_scoped || !subscriber.user_id || event["user_id"].asString() == *subscriber.user_id)) {
                    if (auto connection{ subscriber.connection.lock() }) {
                        connections.push_back(std::move(connection));
                    }
                }
            }
        }
        auto const payload{ wire(event) };
        for (auto const& connection : connections) {
            connection->send(payload);
        }
    }

    /**
     * @brief Close every WebSocket owned by a revoked administrative session.
     * @param session Session cookie value.
     */
    auto revoke_streams(std::string const& session) -> void {
        auto& live{ state() };
        std::vector<drogon::WebSocketConnectionPtr> connections{};
        {
            std::lock_guard lock{ live.clients_mutex };
            for (auto it{ live.clients.begin() }; it != live.clients.end();) {
                if (it->second.session == session) {
                    if (auto connection{ it->second.connection.lock() }) {
                        connections.push_back(std::move(connection));
                    }
                    it = live.clients.erase(it);
                } else {
                    ++it;
                }
            }
        }
        for (auto const& connection : connections) {
            connection->forceClose();
        }
    }

    /**
     * @brief Register a connection with the original absolute authentication expiry.
     * @param request Authenticated upgrade request.
     * @param connection Open WebSocket.
     * @param expires Absolute session expiry.
     * @param logs Whether this connection receives log batches instead of chat events.
     */
    auto open_stream(
        drogon::HttpRequestPtr const& request,
        drogon::WebSocketConnectionPtr const& connection,
        std::chrono::steady_clock::time_point expires,
        bool logs
    ) -> void {
        Subscriber subscriber{ .connection = connection, .session = request->getCookie(ADMIN_SESSION_COOKIE), .expires = expires, .logs = logs };
        {
            std::lock_guard lock{ state().clients_mutex };
            state().clients[connection.get()] = std::move(subscriber);
        }
        connection->send("{\"type\":\"ready\"}");
    }

    /**
     * @brief Validate and apply an event or log subscription command.
     * @param connection Open WebSocket.
     * @param message JSON subscribe command.
     */
    auto subscribe_stream(
        drogon::WebSocketConnectionPtr const& connection,
        std::string const& message
    ) -> void {
        try {
            Json::CharReaderBuilder reader{};
            Json::Value body{};
            std::string errors{};
            std::unique_ptr<Json::CharReader> parser{ reader.newCharReader() };
            if (message.size() > 4096 || !parser->parse(message.data(), message.data() + message.size(), &body, &errors) || !body.isObject() || body["type"] != "subscribe") {
                throw std::invalid_argument{ "expected a subscribe object" };
            }
            Subscriber subscriber{};
            {
                std::lock_guard lock{ state().clients_mutex };
                auto const found{ state().clients.find(connection.get()) };
                if (found == state().clients.end()) {
                    return;
                }
                subscriber = found->second;
            }
            if (subscriber.expires <= std::chrono::steady_clock::now()) {
                connection->forceClose();
                return;
            }
            Json::Value reply{ Json::objectValue };
            if (subscriber.logs) {
                subscriber.subscribed = true;
                for (auto const* key : { "level", "keyword", "qq" }) {
                    if (body.isMember(key) && !body[key].isString()) {
                        throw std::invalid_argument{ "log filters must be strings" };
                    }
                }
                subscriber.filter = { body.get("level", "").asString(), body.get("keyword", "").asString(), body.get("qq", "").asString() };
                validate_log_filter(subscriber.filter);
                if (!subscriber.filter.qq.empty()) {
                    subscriber.filter.qq = std::to_string(workflow::parse_qq(subscriber.filter.qq));
                }
                std::optional<std::int64_t> after{};
                if (body.isMember("after")) {
                    if (!body["after"].isString()) {
                        throw std::invalid_argument{ "after must be a decimal string" };
                    }
                    after = decimal(body["after"].asString());
                }
                reply = read_logs(subscriber.filter, after, 200);
                reply["type"] = "logs";
                subscriber.cursor = decimal(reply["cursor"].asString());
            } else {
                if (body["user_id"].isNull()) {
                    subscriber.user_id.reset();
                } else if (body["user_id"].isString()) {
                    subscriber.user_id = std::to_string(workflow::parse_qq(body["user_id"].asString()));
                } else {
                    throw std::invalid_argument{ "user_id must be a decimal string or null" };
                }
                reply["type"] = "subscribed";
                reply["user_id"] = subscriber.user_id ? Json::Value{ *subscriber.user_id } : Json::Value{};
            }
            {
                std::lock_guard lock{ state().clients_mutex };
                auto const found{ state().clients.find(connection.get()) };
                if (found == state().clients.end()) {
                    return;
                }
                subscriber.revision++;
                found->second = std::move(subscriber);
            }
            connection->send(wire(reply));
        } catch (std::invalid_argument const& error) {
            Json::Value reply{ Json::objectValue };
            reply["type"] = "error";
            reply["error"] = error.what();
            connection->send(wire(reply));
        }
    }

    /**
     * @brief Remove a closed connection from the registry.
     * @param connection Closed WebSocket.
     */
    auto close_stream(drogon::WebSocketConnectionPtr const& connection) -> void {
        std::lock_guard lock{ state().clients_mutex };
        state().clients.erase(connection.get());
    }

    /**
     * @brief Validate a minimum log level, keyword, and optional QQ filter.
     * @param filter Filters from an HTTP or WebSocket request.
     * @throws std::invalid_argument If a filter is invalid.
     */
    auto validate_log_filter(LogFilter const& filter) -> void {
        if (!filter.level.empty()) {
            static_cast<void>(level_index(filter.level));
        }
        if (filter.keyword.size() > 256) {
            throw std::invalid_argument{ "keyword must not exceed 256 bytes" };
        }
        if (!filter.qq.empty()) {
            static_cast<void>(workflow::parse_qq(filter.qq));
        }
    }

    /**
     * @brief Read bounded log records with a cursor that advances across filtered-out entries.
     * @param filter Minimum level, text, and QQ filters.
     * @param after Exclusive cursor; empty returns the latest matching records.
     * @param limit Maximum records to return.
     * @return Entries, cursor, oldest_id, truncation marker, and current logging level.
     */
    auto read_logs(
        LogFilter const& filter,
        std::optional<std::int64_t> after,
        std::size_t limit
    ) -> Json::Value {
        auto& live{ state() };
        auto const minimum{ filter.level.empty() ? 0 : level_index(filter.level) };
        std::lock_guard lock{ live.logs_mutex };
        Json::Value reply{ Json::objectValue };
        reply["entries"] = Json::Value{ Json::arrayValue };
        reply["filters"]["level"] = filter.level;
        reply["filters"]["keyword"] = filter.keyword;
        reply["filters"]["qq"] = filter.qq;
        reply["oldest_id"] = live.records.empty() ? "0" : std::to_string(live.records.front().id);
        reply["truncated"] = after && !live.records.empty() && *after < live.records.front().id - 1;
        reply["log_level"] = LEVELS[static_cast<std::size_t>(trantor::Logger::logLevel())];
        std::int64_t cursor{ after.value_or(live.next_id - 1) };
        std::vector<Json::Value> matching{};
        for (auto const& record : live.records) {
            if (after && record.id <= *after) {
                continue;
            }
            cursor = record.id;
            auto const text{ record.body["message"].asString() };
            if (record.level >= minimum && text.find(filter.keyword) != std::string::npos && matches_qq(text, filter.qq)) {
                matching.push_back(record.body);
                if (after && matching.size() == limit) {
                    break;
                }
            }
        }
        auto const start{ matching.size() > limit ? matching.size() - limit : 0 };
        for (auto i{ start }; i < matching.size(); ++i) {
            reply["entries"].append(std::move(matching[i]));
        }
        reply["cursor"] = std::to_string(cursor);
        return reply;
    }

    /**
     * @brief Change the process logging threshold using Trantor's logging API.
     * @param level TRACE, DEBUG, INFO, WARN, ERROR, or FATAL.
     * @throws std::invalid_argument If the level is unknown.
     */
    auto set_log_level(std::string const& level) -> void {
        trantor::Logger::setLogLevel(static_cast<trantor::Logger::LogLevel>(level_index(level)));
    }

} // namespace admin
