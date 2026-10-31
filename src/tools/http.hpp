/**
 * @file http.hpp
 * @brief Share bounded asynchronous HTTP requests for configured agent integrations.
 */
#pragma once
#include <string>

#include <drogon/utils/coroutine.h>
#include <json/value.h>

namespace tools {
    /**
     * @brief Owned HTTP response for MCP and Lua host operations.
     */
    struct HttpResult {
        int status{};        ///< HTTP status code.
        std::string body;    ///< At most 1 MiB response text.
        Json::Value headers; ///< Lowercase headers, retained only inside the integration.
    };

    /**
     * @brief Send one request without retrying side effects and record bounded diagnostics.
     * @param url HTTP(S) URL without user information or fragment.
     * @param method GET, POST, PUT, PATCH, or DELETE.
     * @param body Owned request text, at most 1 MiB.
     * @param headers String-valued request headers.
     * @param timeout Overall response timeout in seconds.
     * @param source Diagnostic source, mcp or tool.
     * @param session Conversation key, or empty for an admin probe.
     * @return Bounded response; throws on bad input, oversized output, or transport failure.
     */
    auto http_request(
        std::string url,
        std::string method,
        std::string body,
        Json::Value headers,
        double timeout,
        std::string source,
        std::string session = {}
    ) -> drogon::Task<HttpResult>;
} // namespace tools
