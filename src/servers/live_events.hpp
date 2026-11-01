/**
 * @file live_events.hpp
 * @brief Share authenticated event subscriptions and a bounded process log buffer.
 */
#pragma once

#include <chrono>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include <drogon/HttpRequest.h>
#include <drogon/WebSocketConnection.h>
#include <json/value.h>

namespace admin {

    /**
     * @brief Filters shared by buffered and streamed process logs.
     */
    struct LogFilter {
        std::string level;   ///< Minimum severity, or empty for every level.
        std::string keyword; ///< Case-sensitive text substring.
        std::string qq;      ///< Complete decimal number to match in the log text.
    };

    /**
     * @brief Start log capture while preserving the configured output and flush callbacks.
     * @param secrets Values to redact from the administrative log copy.
     * @note Call once before serving requests; stdout/file output remains unchanged.
     */
    auto start_live_events(std::vector<std::string> secrets) -> void;

    /**
     * @brief Stop timers, close subscribers, and disable administrative log capture.
     */
    auto stop_live_events() -> void;

    /**
     * @brief Publish a persisted message, session update, or usage update to matching subscribers.
     * @param event Object with a type and optional decimal string user_id.
     */
    auto publish_event(Json::Value event) -> void;

    /**
     * @brief Close every WebSocket owned by a revoked administrative session.
     * @param session Session cookie value.
     */
    auto revoke_streams(std::string const& session) -> void;

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
    ) -> void;

    /**
     * @brief Validate and apply an event or log subscription command.
     * @param connection Open WebSocket.
     * @param message JSON subscribe command.
     */
    auto subscribe_stream(
        drogon::WebSocketConnectionPtr const& connection,
        std::string const& message
    ) -> void;

    /**
     * @brief Remove a closed connection from the registry.
     * @param connection Closed WebSocket.
     */
    auto close_stream(drogon::WebSocketConnectionPtr const& connection) -> void;

    /**
     * @brief Validate a minimum log level, keyword, and optional QQ filter.
     * @param filter Filters from an HTTP or WebSocket request.
     * @throws std::invalid_argument If a filter is invalid.
     */
    auto validate_log_filter(LogFilter const& filter) -> void;

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
    ) -> Json::Value;

    /**
     * @brief Change the process logging threshold using Trantor's logging API.
     * @param level TRACE, DEBUG, INFO, WARN, ERROR, or FATAL.
     * @throws std::invalid_argument If the level is unknown.
     */
    auto set_log_level(std::string const& level) -> void;

} // namespace admin
