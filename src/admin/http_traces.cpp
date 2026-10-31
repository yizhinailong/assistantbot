/**
 * @file http_traces.cpp
 * @brief Collect redacted request attempts without retaining authorization headers.
 */
#include "admin/http_traces.hpp"

#include <algorithm>
#include <cctype>
#include <deque>
#include <memory>
#include <mutex>
#include <regex>
#include <stdexcept>

#include <drogon/HttpAppFramework.h>

#include "admin/live_events.hpp"
#include "common/time.hpp"

namespace admin {

    namespace {

        /**
         * @brief Process-local request ring and its credential redaction values.
         */
        struct TraceState {
            std::mutex mutex;                 ///< Protects all fields.
            std::deque<Json::Value> records;  ///< At most fifty requests.
            std::vector<std::string> secrets; ///< Configured credentials.
            std::uint64_t next_id{};          ///< Monotonic diagnostic ID.
        };

        /**
         * @brief Return the process request ring.
         * @return Shared synchronized state.
         */
        auto state() -> TraceState& {
            static TraceState value{};
            return value;
        }

        /**
         * @brief Remove credentials and image bytes recursively from JSON diagnostics.
         * @param value Parsed request or response data.
         */
        auto redact_json(Json::Value& value) -> void {
            if (value.isArray()) {
                for (auto& item : value) {
                    redact_json(item);
                }
            } else if (value.isObject()) {
                for (auto const& name : value.getMemberNames()) {
                    auto lowered{ name };
                    std::ranges::transform(lowered, lowered.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
                    if (lowered == "authorization" || lowered == "api_key" || lowered == "access_token" || lowered == "token" || lowered == "secret" || lowered == "cookie" || lowered == "b64_json") {
                        value[name] = "[redacted]";
                    } else {
                        redact_json(value[name]);
                    }
                }
            } else if (value.isString() && value.asString().starts_with("data:image/")) {
                value = "[image omitted]";
            }
        }

        /**
         * @brief Sanitize a bounded diagnostic string, including truncated JSON payloads.
         * @param text Request text, response text, URL, or error summary.
         * @param secrets Configured credential values.
         * @return Redacted textual prefix.
         */
        auto sanitize(std::string text, std::vector<std::string> const& secrets) -> std::string {
            for (auto const& secret : secrets) {
                if (secret.empty()) {
                    continue;
                }
                std::size_t offset{};
                while ((offset = text.find(secret, offset)) != std::string::npos) {
                    text.replace(offset, secret.size(), "[redacted]");
                    offset += 10;
                }
            }
            Json::CharReaderBuilder reader_builder{};
            reader_builder["collectComments"] = false;
            std::unique_ptr<Json::CharReader> reader{ reader_builder.newCharReader() };
            Json::Value parsed{};
            std::string errors{};
            if (reader->parse(text.data(), text.data() + text.size(), &parsed, &errors) && (parsed.isObject() || parsed.isArray())) {
                redact_json(parsed);
                Json::StreamWriterBuilder writer{};
                writer["indentation"] = "";
                text = Json::writeString(writer, parsed);
            }
            static std::regex const QUERY{ R"(([?&](?:api_key|access_token|token|secret|key)=)[^&#\s]*)", std::regex::icase };
            static std::regex const FIELDS{ R"rx(("(?:authorization|api_key|access_token|token|secret|cookie|b64_json)"\s*:\s*")[^"\r\n]*)rx", std::regex::icase };
            static std::regex const IMAGES{ R"(data:image/[^"\s\\]*)", std::regex::icase };
            text = std::regex_replace(text, QUERY, "$1[redacted]");
            text = std::regex_replace(text, FIELDS, "$1[redacted]");
            text = std::regex_replace(text, IMAGES, "[image omitted]");
            if (text.size() > 65536) {
                std::size_t end{ 65536 };
                while (end > 0 && (static_cast<unsigned char>(text[end]) & 0xc0) == 0x80) {
                    --end;
                }
                text.resize(end);
            }
            return text;
        }

        /**
         * @brief Convert a library diagnostic into the administrative record format.
         * @tparam Trace Library HTTP trace type.
         * @param trace Owned library attempt data.
         * @param source model or onebot.
         * @param role Logical model role.
         * @param session Conversation identifier.
         * @return Record ready for redaction and retention.
         */
        template <typename Trace>
        auto trace_json(
            Trace const& trace,
            std::string source,
            std::string role = {},
            std::string session = {}
        ) -> Json::Value {
            Json::Value value{ Json::objectValue };
            value["source"] = std::move(source);
            value["role"] = std::move(role);
            value["session"] = std::move(session);
            value["method"] = trace.method;
            value["url"] = trace.url;
            value["status"] = trace.status_code;
            value["elapsed_ms"] = trace.elapsed_ms;
            value["request_body"] = trace.request_body;
            value["response_body"] = trace.response_body;
            value["error"] = trace.error;
            value["request_truncated"] = trace.request_truncated;
            value["response_truncated"] = trace.response_truncated;
            return value;
        }

    } // namespace

    /**
     * @brief Configure credentials removed from the diagnostic copy of HTTP traffic.
     * @param secrets Configured credential values, never returned to clients.
     */
    auto configure_http_traces(std::vector<std::string> secrets) -> void {
        auto& value{ state() };
        std::lock_guard lock{ value.mutex };
        value.secrets = std::move(secrets);
    }

    /**
     * @brief Append one diagnostic record and announce its ID to administrators.
     * @param trace Owned HTTP metadata and bounded textual payloads.
     */
    auto record_http_trace(Json::Value trace) -> void {
        auto& value{ state() };
        std::vector<std::string> secrets{};
        {
            std::lock_guard lock{ value.mutex };
            secrets = value.secrets;
        }
        trace.removeMember("headers");
        for (auto const* field : { "url", "request_body", "response_body", "error" }) {
            trace[field] = sanitize(trace.get(field, "").asString(), secrets);
        }
        std::string id{};
        {
            std::lock_guard lock{ value.mutex };
            id = std::to_string(++value.next_id);
            trace["id"] = id;
            trace["timestamp"] = Json::Int64{ common::unix_seconds() };
            value.records.push_back(std::move(trace));
            if (value.records.size() > 50) {
                value.records.pop_front();
            }
        }
        Json::Value event{ Json::objectValue };
        event["type"] = "http_trace_created";
        event["id"] = std::move(id);
        publish_event(std::move(event));
    }

    /**
     * @brief Read the most recent matching requests in chronological order.
     * @param source Empty, model, onebot, or mcp.
     * @return Record array and ring capacity.
     * @throws std::invalid_argument If the source is unknown.
     */
    auto read_http_traces(std::string const& source) -> Json::Value {
        if (!source.empty() && source != "model" && source != "onebot" && source != "mcp") {
            throw std::invalid_argument{ "source must be model, onebot, or mcp" };
        }
        Json::Value result{ Json::objectValue };
        result["capacity"] = 50;
        result["traces"] = Json::Value{ Json::arrayValue };
        auto& value{ state() };
        std::lock_guard lock{ value.mutex };
        for (auto const& record : value.records) {
            if (source.empty() || record["source"].asString() == source) {
                result["traces"].append(record);
            }
        }
        return result;
    }

    /**
     * @brief Clear retained requests without resetting monotonic record IDs.
     */
    auto clear_http_traces() -> void {
        auto& value{ state() };
        std::lock_guard lock{ value.mutex };
        value.records.clear();
    }

    /**
     * @brief Copy a model client with bounded diagnostic observation enabled.
     * @param client Original immutable model client.
     * @param role Logical model role for request inspection.
     * @param session Conversation identifier when available.
     * @return Client sharing the original settings with a diagnostic observer.
     */
    auto traced_model(
        drogon_openai::OpenAI client,
        std::string role,
        std::string session
    ) -> drogon_openai::OpenAI {
        return client.WithOptions({ .http_trace = [role = std::move(role), session = std::move(session)](drogon_openai::HttpTrace const& trace) {
            record_http_trace(trace_json(trace, "model", role, session));
        } });
    }

    /**
     * @brief Create a typed OneBot client with HTTP diagnostics enabled.
     * @return Configured plugin client; reverse WebSocket actions emit no HTTP record.
     */
    auto traced_onebot() -> drogon_onebot::OneBot {
        return drogon::app().getPlugin<drogon_onebot::OneBotClient>()->NewOneBotClient([](drogon_onebot::HttpTrace const& trace) {
            record_http_trace(trace_json(trace, "onebot"));
        });
    }

} // namespace admin
