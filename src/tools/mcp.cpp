/**
 * @file mcp.cpp
 * @brief Support legacy MCP handshakes and 2026 stateless metadata over JSON or SSE responses.
 */
#include "tools/mcp.hpp"

#include <cctype>
#include <regex>
#include <set>
#include <sstream>
#include <stdexcept>

#include <drogon/drogon.h>
#include <drogon/utils/Utilities.h>

#include "admin/auth.hpp"
#include "admin/http_traces.hpp"
#include "common/json.hpp"
#include "tools/http.hpp"

namespace {
    /**
     * @brief Encode MCP mirrored values using the required Base64 sentinel when needed.
     * @param text Original UTF-8 value.
     * @return Header-safe ASCII representation.
     */
    auto header_value(std::string const& text) -> std::string {
        bool safe{ true };
        for (auto ch : text) {
            safe = safe && ((static_cast<unsigned char>(ch) >= 0x20 && static_cast<unsigned char>(ch) <= 0x7e) || ch == '\t');
        }
        if (!text.empty() && (std::isspace(static_cast<unsigned char>(text.front())) || std::isspace(static_cast<unsigned char>(text.back())))) {
            safe = false;
        }
        if (text.starts_with("=?base64?") && text.ends_with("?=")) {
            safe = false;
        }
        return safe ? text : "=?base64?" + drogon::utils::base64Encode(text) + "?=";
    }

    /**
     * @brief Validate header annotations at statically reachable primitive properties.
     * @param schema Current schema node.
     * @param property Whether this node is reached exclusively through properties.
     * @param reachable Whether its ancestry remains statically reachable.
     * @param names Case-insensitive annotation names already encountered.
     * @param depth Schema nesting limit.
     * @return Whether every annotation is valid.
     */
    auto valid_headers(
        Json::Value const& schema,
        bool property,
        bool reachable,
        std::set<std::string>& names,
        int depth = 0
    ) -> bool {
        if (depth > 24) {
            return false;
        }
        if (schema.isArray()) {
            for (auto const& child : schema) {
                if (!valid_headers(child, false, false, names, depth + 1)) {
                    return false;
                }
            }
            return true;
        }
        if (!schema.isObject()) {
            return true;
        }
        if (schema.isMember("x-mcp-header")) {
            auto const& value{ schema["x-mcp-header"] };
            if (!property || !reachable || !value.isString() || value.asString().size() > 128) {
                return false;
            }
            static std::regex const TOKEN{ R"([!#$%&'*+.^_`|~A-Za-z0-9-]+)" };
            auto name{ value.asString() };
            if (!std::regex_match(name, TOKEN)) {
                return false;
            }
            for (auto& ch : name) {
                ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
            }
            if (!names.insert(name).second || (schema["type"] != "string" && schema["type"] != "boolean" && schema["type"] != "integer")) {
                return false;
            }
        }
        for (auto const& key : schema.getMemberNames()) {
            if (key == "properties" && schema[key].isObject()) {
                for (auto const& name : schema[key].getMemberNames()) {
                    if (!valid_headers(schema[key][name], true, reachable, names, depth + 1)) {
                        return false;
                    }
                }
            } else if (!valid_headers(schema[key], false, false, names, depth + 1)) {
                return false;
            }
        }
        return true;
    }

    /**
     * @brief Mirror annotated argument values into safe MCP request headers.
     * @param schema Validated tool schema.
     * @param value Arguments at the matching property path.
     * @param headers Output HTTP header object.
     */
    auto parameter_headers(
        Json::Value const& schema,
        Json::Value const& value,
        Json::Value& headers
    ) -> void {
        if (!schema.isObject()) {
            return;
        }
        if (schema.isMember("x-mcp-header") && !value.isNull()) {
            std::string text{};
            if (schema["type"] == "string" && value.isString()) {
                text = value.asString();
            } else if (schema["type"] == "boolean" && value.isBool()) {
                text = value.asBool() ? "true" : "false";
            } else if (schema["type"] == "integer" && value.isInt64() && value.asInt64() >= -9007199254740991LL && value.asInt64() <= 9007199254740991LL) {
                text = std::to_string(value.asInt64());
            } else {
                throw std::invalid_argument{ "MCP mirrored argument has the wrong primitive type or integer range" };
            }
            headers["Mcp-Param-" + schema["x-mcp-header"].asString()] = header_value(text);
        }
        if (schema["properties"].isObject() && value.isObject()) {
            for (auto const& name : schema["properties"].getMemberNames()) {
                if (value.isMember(name)) {
                    parameter_headers(schema["properties"][name], value[name], headers);
                }
            }
        }
    }
} // namespace

namespace tools {
    /**
     * @brief Resolve one configured service without making a network request.
     * @param config Validated service document.
     * @param conversation Local conversation key for diagnostics.
     */
    McpSession::McpSession(Json::Value config, std::string conversation)
        : m_config{ std::move(config) }, m_conversation{ std::move(conversation) } {
        if (!m_config["token_env"].asString().empty()) {
            m_token = admin::read_env(m_config["token_env"].asString());
            if (m_token.empty()) {
                throw std::runtime_error{ "MCP token environment variable is unset" };
            }
            admin::add_http_trace_secret(m_token);
        }
    }

    /**
     * @brief Send and validate one JSON-RPC request or legacy notification.
     * @param method RPC method.
     * @param params Owned method parameters.
     * @param notification Whether the request omits its ID.
     * @return Matching RPC result or an exception.
     */
    auto McpSession::rpc(
        std::string method,
        Json::Value params,
        bool notification
    ) -> drogon::Task<Json::Value> {
        bool const modern{ m_config["protocol_version"] == "2026-07-28" };
        Json::Value body{};
        body["jsonrpc"] = "2.0";
        body["method"] = method;
        body["params"] = std::move(params);
        auto const id{ m_nextId++ };
        if (!notification) {
            body["id"] = Json::Int64{ id };
        }
        Json::Value headers{};
        headers["Content-Type"] = "application/json";
        headers["Accept"] = "application/json, text/event-stream";
        headers["MCP-Protocol-Version"] = m_config["protocol_version"];
        if (!m_token.empty()) {
            headers["Authorization"] = "Bearer " + m_token;
        }
        if (!m_session.empty()) {
            headers["Mcp-Session-Id"] = m_session;
        }
        if (modern) {
            auto& meta{ body["params"]["_meta"] };
            meta["io.modelcontextprotocol/protocolVersion"] = m_config["protocol_version"];
            meta["io.modelcontextprotocol/clientInfo"]["name"] = "assistantbot";
            meta["io.modelcontextprotocol/clientInfo"]["version"] = ASSISTANTBOT_VERSION;
            meta["io.modelcontextprotocol/clientCapabilities"] = Json::Value{ Json::objectValue };
            headers["Mcp-Method"] = method;
            if (method == "tools/call") {
                auto const name{ body["params"]["name"].asString() };
                headers["Mcp-Name"] = header_value(name);
                if (m_catalog.isMember(name)) {
                    parameter_headers(m_catalog[name], body["params"]["arguments"], headers);
                }
            }
        }
        auto const response{ co_await http_request(m_config["url"].asString(), "POST", common::json_text(body), std::move(headers), m_config["timeout"].asDouble(), "mcp", m_conversation) };
        if (notification) {
            if (response.status != 202 && response.status != 200 && response.status != 204) {
                throw std::runtime_error{ "MCP initialization notification was rejected" };
            }
            co_return Json::Value{};
        }
        Json::Value reply{};
        if (response.headers.get("content-type", "").asString().starts_with("text/event-stream")) {
            std::istringstream stream{ response.body };
            std::string line{}, data{};
            auto consume{ [&] {
                if (data.empty()) {
                    return;
                }
                auto item{ common::parse_json(data) };
                if (item.isObject() && item["id"] == body["id"]) {
                    reply = std::move(item);
                }
                data.clear();
            } };
            while (std::getline(stream, line)) {
                if (!line.empty() && line.back() == '\r') {
                    line.pop_back();
                }
                if (line.empty()) {
                    consume();
                    continue;
                }
                if (line.starts_with("data:")) {
                    if (!data.empty()) {
                        data += '\n';
                    }
                    data += line.substr(line.size() > 5 && line[5] == ' ' ? 6 : 5);
                }
            }
            consume();
        } else if (!response.body.empty()) {
            reply = common::parse_json(response.body);
        }
        if (response.status < 200 || response.status >= 300) {
            throw std::runtime_error{ "MCP request failed with HTTP " + std::to_string(response.status) };
        }
        if (!reply.isObject() || reply["jsonrpc"] != "2.0" || reply["id"] != body["id"]) {
            throw std::runtime_error{ "MCP returned no matching JSON-RPC response" };
        }
        if (reply.isMember("error")) {
            throw std::runtime_error{ "MCP RPC error: " + reply["error"].get("message", "remote error").asString().substr(0, 2048) };
        }
        if (!reply["result"].isObject() || reply["result"].isMember("inputRequests")) {
            throw std::runtime_error{ "MCP result is invalid or requires unsupported client input" };
        }
        if (!modern && method == "initialize" && response.headers.isMember("mcp-session-id")) {
            auto const session{ response.headers["mcp-session-id"].asString() };
            if (session.empty() || session.size() > 1024) {
                throw std::runtime_error{ "invalid MCP session identifier" };
            }
            for (auto ch : session) {
                if (static_cast<unsigned char>(ch) < 0x21 || static_cast<unsigned char>(ch) > 0x7e) {
                    throw std::runtime_error{ "invalid MCP session identifier" };
                }
            }
            m_session = session;
        }
        co_return reply["result"];
    }

    /**
     * @brief Initialize legacy sessions; modern requests carry metadata independently.
     * @return Task completing once this context is ready.
     */
    auto McpSession::initialize() -> drogon::Task<> {
        if (m_ready) {
            co_return;
        }
        if (m_config["protocol_version"] == "2025-11-25") {
            Json::Value params{};
            params["protocolVersion"] = m_config["protocol_version"];
            params["capabilities"] = Json::Value{ Json::objectValue };
            params["clientInfo"]["name"] = "assistantbot";
            params["clientInfo"]["version"] = ASSISTANTBOT_VERSION;
            auto const result{ co_await rpc("initialize", std::move(params)) };
            if (result["protocolVersion"] != m_config["protocol_version"]) {
                throw std::runtime_error{ "MCP server selected an unsupported protocol version" };
            }
            co_await rpc("notifications/initialized", Json::Value{ Json::objectValue }, true);
        }
        m_ready = true;
    }

    /**
     * @brief Discover bounded tools, following distinct pagination cursors.
     * @return At most 256 validated tool definitions.
     */
    auto McpSession::List() -> drogon::Task<Json::Value> {
        co_await initialize();
        Json::Value tools{ Json::arrayValue };
        std::set<std::string> cursors{}, names{};
        std::string cursor{};
        for (int page{}; page < 20; ++page) {
            Json::Value params{ Json::objectValue };
            if (!cursor.empty()) {
                params["cursor"] = cursor;
            }
            auto const result{ co_await rpc("tools/list", std::move(params)) };
            if (!result["tools"].isArray()) {
                throw std::runtime_error{ "MCP tools/list did not return tools" };
            }
            for (auto const& tool : result["tools"]) {
                if (!tool.isObject() || !tool["name"].isString() || tool["name"].asString().empty() || tool["name"].asString().size() > 256 || !tool["inputSchema"].isObject() || !names.insert(tool["name"].asString()).second || tools.size() >= 256) {
                    throw std::runtime_error{ "MCP tool catalogue is invalid or exceeds 256 tools" };
                }
                if (m_config["protocol_version"] == "2026-07-28") {
                    std::set<std::string> header_names{};
                    if (!valid_headers(tool["inputSchema"], false, true, header_names)) {
                        LOG_WARN << "Ignoring MCP tool with invalid header annotations";
                        continue;
                    }
                }
                m_catalog[tool["name"].asString()] = tool["inputSchema"];
                tools.append(tool);
            }
            if (!result.isMember("nextCursor") || result["nextCursor"].isNull()) {
                co_return tools;
            }
            if (!result["nextCursor"].isString() || result["nextCursor"].asString().empty() || !cursors.insert(result["nextCursor"].asString()).second) {
                throw std::runtime_error{ "MCP pagination cursor is invalid or repeated" };
            }
            cursor = result["nextCursor"].asString();
        }
        throw std::runtime_error{ "MCP discovery exceeds 20 pages" };
    }

    /**
     * @brief Call a remote tool once without retrying side effects.
     * @param name Original MCP tool name.
     * @param arguments JSON argument object.
     * @return Remote tool result, including isError when reported by the server.
     */
    auto McpSession::Call(std::string name, Json::Value arguments) -> drogon::Task<Json::Value> {
        if (name.empty() || name.size() > 256 || !arguments.isObject()) {
            throw std::invalid_argument{ "MCP tool calls require a name and argument object" };
        }
        co_await initialize();
        if (m_config["protocol_version"] == "2026-07-28" && !m_catalog.isMember(name)) {
            co_await List();
            if (!m_catalog.isMember(name)) {
                throw std::invalid_argument{ "MCP tool is absent from the validated catalogue" };
            }
        }
        Json::Value params{};
        params["name"] = std::move(name);
        params["arguments"] = std::move(arguments);
        co_return co_await rpc("tools/call", std::move(params));
    }

    /**
     * @brief Close a legacy remote session, ignoring unsupported deletion or shutdown failures.
     * @return Task completing after the best-effort close.
     */
    auto McpSession::Close() -> drogon::Task<> {
        if (m_session.empty()) {
            co_return;
        }
        Json::Value headers{};
        headers["Mcp-Session-Id"] = m_session;
        headers["MCP-Protocol-Version"] = m_config["protocol_version"];
        if (!m_token.empty()) {
            headers["Authorization"] = "Bearer " + m_token;
        }
        m_session.clear();
        try {
            co_await http_request(m_config["url"].asString(), "DELETE", "", std::move(headers), 5.0, "mcp", m_conversation);
        } catch (std::exception const&) {}
    }
} // namespace tools
