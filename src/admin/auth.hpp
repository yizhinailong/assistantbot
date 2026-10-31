/**
 * @file auth.hpp
 * @brief Declare administrative authentication and expiring in-memory sessions.
 */
#pragma once

#include <chrono>
#include <mutex>
#include <string>
#include <unordered_map>

#include <drogon/HttpRequest.h>
#include <drogon/WebSocketConnection.h>
#include <drogon/plugins/Plugin.h>
#include <json/value.h>

namespace admin {

    constexpr char const* ADMIN_SESSION_COOKIE{ "assistantbot_admin_session" }; ///< Host-only administrative cookie.

    /**
     * @brief Authenticate the administrative API with an environment secret and opaque sessions.
     */
    class Auth : public drogon::Plugin<Auth> {
    private:
        std::string m_token;                                                               ///< Secret loaded once, never returned or logged.
        mutable std::mutex m_mutex;                                                        ///< Guards the secret and session registry.
        std::unordered_map<std::string, std::chrono::steady_clock::time_point> m_sessions; ///< Expiry by session ID.

    public:
        /**
         * @brief Load admin_token_env and register the administrative request guard.
         * @param config Object containing the environment variable name.
         * @throws std::invalid_argument If the name or secret is missing or empty.
         */
        auto initAndStart(Json::Value const& config) -> void override;
        /**
         * @brief Revoke all sessions on shutdown.
         */
        auto shutdown() -> void override;
        /**
         * @brief Check a session against its absolute twelve-hour expiry.
         * @param session Opaque cookie value.
         * @return Whether the session is valid.
         */
        [[nodiscard]] auto Authenticated(std::string const& session) const -> bool;
        /**
         * @brief Authorize and register a stream atomically with session revocation.
         * @param request Upgrade request carrying the administrative cookie.
         * @param connection Established WebSocket connection.
         * @param logs Whether the connection subscribes to process logs.
         * @return Whether the session was valid and the stream was registered.
         */
        [[nodiscard]] auto AttachStream(
            drogon::HttpRequestPtr const& request,
            drogon::WebSocketConnectionPtr const& connection,
            bool logs
        ) const -> bool;
        /**
         * @brief Verify the secret and create a new session.
         * @param token Submitted secret.
         * @return Random session ID, or empty for an incorrect token.
         */
        [[nodiscard]] auto Login(std::string const& token) -> std::string;
        /**
         * @brief Revoke a session immediately.
         * @param session Opaque cookie value.
         */
        auto Logout(std::string const& session) -> void;
    };

    /**
     * @brief Read an environment variable portably.
     * @param name Variable name.
     * @return Value, or empty if unset.
     */
    [[nodiscard]] auto read_env(std::string const& name) -> std::string;
    /**
     * @brief Compare fixed-size SHA-256 digests without a content-dependent early exit.
     * @param left First token.
     * @param right Second token.
     * @return Whether the tokens match.
     */
    [[nodiscard]] auto constant_time_equal(
        std::string const& left,
        std::string const& right
    ) noexcept -> bool;

} // namespace admin
