/**
 * @file mcp.hpp
 * @brief Implement bounded Streamable HTTP MCP sessions for one model reply or admin probe.
 */
#pragma once
#include <string>

#include <drogon/utils/coroutine.h>
#include <json/value.h>

namespace tools {
    /**
     * @brief Own a legacy handshake session or a modern stateless MCP connection context.
     */
    class McpSession {
    private:
        Json::Value m_config;                       ///< Immutable service configuration with environment names only.
        std::string m_token;                        ///< Resolved token, never exposed to the admin API.
        std::string m_session;                      ///< Legacy remote session identifier.
        std::string m_conversation;                 ///< Local diagnostic conversation key.
        std::int64_t m_nextId{ 1 };                 ///< Sequential request identifiers within this reply.
        Json::Value m_catalog{ Json::objectValue }; ///< Discovered schemas for modern parameter headers.
        bool m_ready{};                             ///< Legacy initialization has completed or modern metadata is ready.

    public:
        /**
         * @brief Resolve one configured service without making a network request.
         * @param config Validated service document.
         * @param conversation Local conversation key for diagnostics.
         */
        McpSession(Json::Value config, std::string conversation = {});
        /**
         * @brief Discover bounded tools, following distinct pagination cursors.
         * @return At most 256 validated tool definitions.
         */
        auto List() -> drogon::Task<Json::Value>;
        /**
         * @brief Call a remote tool once without retrying side effects.
         * @param name Original MCP tool name.
         * @param arguments JSON argument object.
         * @return Remote tool result, including isError when reported by the server.
         */
        auto Call(std::string name, Json::Value arguments) -> drogon::Task<Json::Value>;
        /**
         * @brief Close a legacy remote session, ignoring unsupported deletion or shutdown failures.
         * @return Task completing after the best-effort close.
         */
        auto Close() -> drogon::Task<>;

    private:
        /**
         * @brief Initialize legacy sessions; modern requests carry metadata independently.
         * @return Task completing once this context is ready.
         */
        auto initialize() -> drogon::Task<>;
        /**
         * @brief Send and validate one JSON-RPC request or legacy notification.
         * @param method RPC method.
         * @param params Owned method parameters.
         * @param notification Whether the request omits its ID.
         * @return Matching RPC result or an exception.
         */
        auto rpc(
            std::string method,
            Json::Value params,
            bool notification = false
        ) -> drogon::Task<Json::Value>;
    };
} // namespace tools
