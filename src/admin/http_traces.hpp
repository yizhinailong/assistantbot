/**
 * @file http_traces.hpp
 * @brief Retain bounded, redacted diagnostics for outgoing HTTP requests.
 */
#pragma once

#include <string>
#include <vector>

#include <drogon_onebot/onebot_client.hpp>
#include <drogon_openai/openai_client.hpp>

namespace admin {

    /**
     * @brief Configure credentials removed from the diagnostic copy of HTTP traffic.
     * @param secrets Configured credential values, never returned to clients.
     */
    auto configure_http_traces(std::vector<std::string> secrets) -> void;

    /**
     * @brief Append one diagnostic record and announce its ID to administrators.
     * @param trace Owned HTTP metadata and bounded textual payloads.
     */
    auto record_http_trace(Json::Value trace) -> void;

    /**
     * @brief Read the most recent matching requests in chronological order.
     * @param source Empty, model, onebot, or mcp.
     * @return Record array and ring capacity.
     * @throws std::invalid_argument If the source is unknown.
     */
    auto read_http_traces(std::string const& source = {}) -> Json::Value;

    /**
     * @brief Clear retained requests without resetting monotonic record IDs.
     */
    auto clear_http_traces() -> void;

    /**
     * @brief Copy a model client with bounded diagnostic observation enabled.
     * @param client Original immutable model client.
     * @param role Logical model role for request inspection.
     * @param session Conversation identifier when available.
     * @return Client sharing the original settings with a diagnostic observer.
     */
    auto traced_model(
        drogon_openai::OpenAI client,
        std::string role = "executor",
        std::string session = {}
    ) -> drogon_openai::OpenAI;

    /**
     * @brief Create a typed OneBot client with HTTP diagnostics enabled.
     * @return Configured plugin client; reverse WebSocket actions emit no HTTP record.
     */
    auto traced_onebot() -> drogon_onebot::OneBot;

} // namespace admin
