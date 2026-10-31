/**
 * @file auth.cpp
 * @brief Implement the administrative authentication service.
 */
#include "admin/auth.hpp"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <memory>
#include <stdexcept>
#include <utility>
#include <vector>

#include <drogon/drogon.h>
#include <trantor/utils/Utilities.h>

#include <drogon_onebot/onebot_client.hpp>
#include <drogon_openai/openai_client.hpp>

#include "admin/http_traces.hpp"
#include "admin/live_events.hpp"

namespace admin {

    /**
     * @brief Read an environment variable portably.
     * @param name Variable name.
     * @return Value, or empty if unset.
     */
    auto read_env(std::string const& name) -> std::string {
#ifdef _WIN32
        char* value{};
        std::size_t size{};
        auto const error{ _dupenv_s(&value, &size, name.c_str()) };
        std::unique_ptr<char, decltype(&std::free)> owned{ value, &std::free };
        return error != 0 || value == nullptr ? std::string{} : std::string{ value };
#else
        auto const* value{ std::getenv(name.c_str()) };
        return value == nullptr ? std::string{} : std::string{ value };
#endif
    }

    /**
     * @brief Compare fixed-size SHA-256 digests without a content-dependent early exit.
     * @param left First token.
     * @param right Second token.
     * @return Whether the tokens match.
     */
    auto constant_time_equal(std::string const& left, std::string const& right) noexcept -> bool {
        auto const a{ trantor::utils::sha256(left) };
        auto const b{ trantor::utils::sha256(right) };
        volatile unsigned char difference{ static_cast<unsigned char>(left.size() != right.size()) };
        for (std::size_t i{}; i < sizeof(a.bytes); ++i) {
            difference = static_cast<unsigned char>(difference | (a.bytes[i] ^ b.bytes[i]));
        }
        return difference == 0;
    }

    /**
     * @brief Load admin_token_env and register the administrative request guard.
     * @param config Object containing the environment variable name.
     * @throws std::invalid_argument If the name or secret is missing or empty.
     */
    auto Auth::initAndStart(Json::Value const& config) -> void {
        if (!config.isObject() || !config["admin_token_env"].isString() || config["admin_token_env"].asString().empty()) {
            throw std::invalid_argument{ "admin::Auth: admin_token_env must be a non-empty environment variable name" };
        }
        m_token = read_env(config["admin_token_env"].asString());
        if (m_token.empty() || m_token.size() > 4096) {
            throw std::invalid_argument{ "admin::Auth: the environment variable named by admin_token_env must contain 1 to 4096 bytes" };
        }
        auto secrets{ std::vector<std::string>{ m_token } };
        drogon::app().registerBeginningAdvice([secrets = std::move(secrets)]() mutable {
            if (auto const* onebot{ drogon::app().getPlugin<drogon_onebot::OneBotClient>() }) {
                secrets.push_back(onebot->Config().access_token);
                secrets.push_back(onebot->Config().secret);
            }
            if (auto const* openai{ drogon::app().getPlugin<drogon_openai::OpenAIClient>() }) {
                secrets.push_back(openai->Config().api_key);
                secrets.push_back(openai->Config().webhook_secret);
            }
            configure_http_traces(secrets);
            start_live_events(std::move(secrets));
        });
        drogon::app().registerPreRoutingAdvice(
            [](drogon::HttpRequestPtr const& request, drogon::AdviceCallback&& callback, drogon::AdviceChainCallback&& chain) {
                auto const path{ request->path() };
                auto normalized_path{ path };
                std::ranges::transform(normalized_path, normalized_path.begin(), [](unsigned char character) { return static_cast<char>(std::tolower(character)); });
                auto const exempt{ normalized_path == "/api/admin/health" || normalized_path == "/api/admin/auth/login" || normalized_path == "/api/admin/auth/status" };
                if (!(normalized_path == "/api/admin" || normalized_path.starts_with("/api/admin/")) || exempt) {
                    chain();
                    return;
                }
                auto const* auth{ drogon::app().getPlugin<Auth>() };
                if (auth != nullptr && auth->Authenticated(request->getCookie(ADMIN_SESSION_COOKIE))) {
                    auto const origin{ request->getHeader("origin") };
                    auto const host{ request->getHeader("host") };
                    if (request->getHeader("upgrade") == "websocket" && !origin.empty() && origin != "http://" + host && origin != "https://" + host) {
                        Json::Value body{ Json::objectValue };
                        body["error"] = "WebSocket origin must match the request host";
                        auto response{ drogon::HttpResponse::newHttpJsonResponse(body) };
                        response->setStatusCode(drogon::k403Forbidden);
                        callback(response);
                        return;
                    }
                    chain();
                    return;
                }
                Json::Value body{ Json::objectValue };
                body["error"] = "authentication required";
                auto response{ drogon::HttpResponse::newHttpJsonResponse(body) };
                response->setStatusCode(drogon::k401Unauthorized);
                response->addHeader("Cache-Control", "no-store");
                callback(response);
            }
        );
        drogon::app().registerPreSendingAdvice(
            [](drogon::HttpRequestPtr const& request, drogon::HttpResponsePtr const& response) {
                auto path{ request->path() };
                std::ranges::transform(path, path.begin(), [](unsigned char character) { return static_cast<char>(std::tolower(character)); });
                if (path == "/api/admin" || path.starts_with("/api/admin/")) {
                    response->addHeader("Cache-Control", "no-store");
                    response->addHeader("X-Content-Type-Options", "nosniff");
                }
            }
        );
    }

    /**
     * @brief Revoke all sessions on shutdown.
     */
    auto Auth::shutdown() -> void {
        {
            std::lock_guard lock{ m_mutex };
            m_sessions.clear();
            m_token.clear();
        }
        stop_live_events();
    }

    /**
     * @brief Check a session against its absolute twelve-hour expiry.
     * @param session Opaque cookie value.
     * @return Whether the session is valid.
     */
    auto Auth::Authenticated(std::string const& session) const -> bool {
        std::lock_guard lock{ m_mutex };
        auto const found{ m_sessions.find(session) };
        return found != m_sessions.end() && found->second > std::chrono::steady_clock::now();
    }

    /**
     * @brief Verify the secret and create a new session.
     * @param token Submitted secret.
     * @return Random session ID, or empty for an incorrect token.
     */
    auto Auth::Login(std::string const& token) -> std::string {
        std::lock_guard lock{ m_mutex };
        if (m_token.empty() || !constant_time_equal(token, m_token)) {
            return {};
        }
        auto const now{ std::chrono::steady_clock::now() };
        std::erase_if(m_sessions, [now](auto const& entry) { return entry.second <= now; });
        if (m_sessions.size() >= 256) {
            auto const oldest{ std::min_element(m_sessions.begin(), m_sessions.end(), [](auto const& a, auto const& b) { return a.second < b.second; }) };
            revoke_streams(oldest->first);
            m_sessions.erase(oldest);
        }
        auto session{ drogon::utils::secureRandomString(32) };
        m_sessions.emplace(session, now + std::chrono::hours{ 12 });
        return session;
    }

    /**
     * @brief Revoke a session immediately.
     * @param session Opaque cookie value.
     */
    auto Auth::Logout(std::string const& session) -> void {
        std::lock_guard lock{ m_mutex };
        m_sessions.erase(session);
        revoke_streams(session);
    }

    /**
     * @brief Authorize and register a stream atomically with session revocation.
     * @param request Upgrade request carrying the administrative cookie.
     * @param connection Established WebSocket connection.
     * @param logs Whether the connection subscribes to process logs.
     * @return Whether the session was valid and the stream was registered.
     */
    auto Auth::AttachStream(
        drogon::HttpRequestPtr const& request,
        drogon::WebSocketConnectionPtr const& connection,
        bool logs
    ) const -> bool {
        std::lock_guard lock{ m_mutex };
        auto const found{ m_sessions.find(request->getCookie(ADMIN_SESSION_COOKIE)) };
        if (found == m_sessions.end() || found->second <= std::chrono::steady_clock::now()) {
            return false;
        }
        open_stream(request, connection, found->second, logs);
        return true;
    }

} // namespace admin
