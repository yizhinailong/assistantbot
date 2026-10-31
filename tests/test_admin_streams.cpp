/**
 * @file test_admin_streams.cpp
 * @brief Exercise real WebSocket subscriptions, log catch-up, and session revocation.
 */
#include <algorithm>
#include <chrono>
#include <condition_variable>
#include <deque>
#include <future>
#include <memory>
#include <mutex>
#include <optional>
#include <stdexcept>
#include <string>

#include <drogon/WebSocketClient.h>
#include <drogon/drogon_test.h>
#include <json/json.h>

#include "admin/live_events.hpp"
#include "admin_support.hpp"

namespace {

    /**
     * @brief Blocking test wrapper whose callbacks retain no pointer to the wrapper.
     */
    class StreamClient {
    private:
        /**
         * @brief Callback state retained until the socket actually closes.
         */
        struct State {
            std::mutex mutex;                 ///< Guards frames and close state.
            std::condition_variable ready;    ///< Signals frames or closure.
            std::deque<Json::Value> messages; ///< Parsed text frames.
            bool closed{};                    ///< Whether the connection closed.
        };

        std::shared_ptr<State> m_state{ std::make_shared<State>() }; ///< Callback-owned state.
        drogon::WebSocketClientPtr m_client;                         ///< Client running on the app's main loop.

    public:
        /**
         * @brief Connect with a valid administrative cookie and wait for the upgrade.
         * @param path Stream route.
         * @param session Administrative session cookie.
         */
        StreamClient(std::string const& path, std::string const& session)
            : m_client{ drogon::WebSocketClient::newWebSocketClient("ws://127.0.0.1:18082") } {
            m_client->setMessageHandler([state = m_state](std::string&& text, auto const&, auto const& type) {
                if (type != drogon::WebSocketMessageType::Text) {
                    return;
                }
                Json::CharReaderBuilder reader{};
                std::unique_ptr<Json::CharReader> parser{ reader.newCharReader() };
                Json::Value message{};
                std::string errors{};
                if (!parser->parse(text.data(), text.data() + text.size(), &message, &errors)) {
                    return;
                }
                {
                    std::lock_guard lock{ state->mutex };
                    state->messages.push_back(std::move(message));
                }
                state->ready.notify_all();
            });
            m_client->setConnectionClosedHandler([state = m_state](auto const&) {
                {
                    std::lock_guard lock{ state->mutex };
                    state->closed = true;
                }
                state->ready.notify_all();
            });
            auto request{ drogon::HttpRequest::newHttpRequest() };
            request->setPath(path);
            request->addCookie(admin::ADMIN_SESSION_COOKIE, session);
            auto connected{ std::make_shared<std::promise<bool>>() };
            auto future{ connected->get_future() };
            m_client->connectToServer(request, [connected](auto result, auto const& response, auto const&) {
                connected->set_value(result == drogon::ReqResult::Ok && response->statusCode() == drogon::k101SwitchingProtocols);
            });
            if (future.wait_for(std::chrono::seconds{ 3 }) != std::future_status::ready || !future.get()) {
                m_client->stop();
                throw std::runtime_error{ "WebSocket connection failed" };
            }
        }

        /**
         * @brief Stop the socket on its own event loop.
         */
        ~StreamClient() {
            m_client->getLoop()->queueInLoop([client = m_client] { client->stop(); });
        }

        /**
         * @brief Send a JSON subscription from the client's event loop.
         * @param message Wire document.
         */
        auto Send(Json::Value const& message) -> void {
            auto text{ Json::writeString(Json::StreamWriterBuilder{}, message) };
            m_client->getLoop()->queueInLoop([client = m_client, text = std::move(text)] {
                if (auto connection{ client->getConnection() }) {
                    connection->send(text);
                }
            });
        }

        /**
         * @brief Wait for the next event of a given type.
         * @param type Event type.
         * @param timeout Maximum wait.
         * @return Matching event, or empty after timeout.
         */
        auto Pop(
            std::string const& type,
            std::chrono::milliseconds timeout = std::chrono::seconds{ 3 }
        ) -> std::optional<Json::Value> {
            std::unique_lock lock{ m_state->mutex };
            auto matches{ [&type](Json::Value const& message) { return message["type"] == type; } };
            m_state->ready.wait_for(lock, timeout, [&] { return m_state->closed || std::ranges::any_of(m_state->messages, matches); });
            auto const found{ std::ranges::find_if(m_state->messages, matches) };
            if (found == m_state->messages.end()) {
                return std::nullopt;
            }
            auto message{ std::move(*found) };
            m_state->messages.erase(found);
            return message;
        }

        /**
         * @brief Wait for revocation to close the connection.
         * @return Whether the stream closed within three seconds.
         */
        auto Closed() -> bool {
            std::unique_lock lock{ m_state->mutex };
            return m_state->ready.wait_for(lock, std::chrono::seconds{ 3 }, [&] { return m_state->closed; });
        }
    };

    /**
     * @brief Build a friend-scoped application event.
     * @param qq Friend identifier.
     * @return Event document.
     */
    auto event_for(char const* qq) -> Json::Value {
        Json::Value event{ Json::objectValue };
        event["type"] = "message_created";
        event["user_id"] = qq;
        return event;
    }

} // namespace

DROGON_TEST(StreamSubscriptionsFilterFriendsAndLogoutClosesTheConnection) {
    auto const cookie{ test_support::login_admin() };
    StreamClient stream{ "/api/admin/ws", cookie };
    REQUIRE(stream.Pop("ready").has_value());
    Json::Value subscription{ Json::objectValue };
    subscription["type"] = "subscribe";
    subscription["user_id"] = "0012345";
    stream.Send(subscription);
    auto const ack{ stream.Pop("subscribed") };
    REQUIRE(ack.has_value());
    CHECK((*ack)["user_id"] == "12345");
    admin::publish_event(event_for("54321"));
    admin::publish_event(event_for("12345"));
    auto const received{ stream.Pop("message_created") };
    REQUIRE(received.has_value());
    CHECK((*received)["user_id"] == "12345");
    CHECK(!stream.Pop("message_created", std::chrono::milliseconds{ 200 }));
    auto usage{ event_for("54321") };
    usage["type"] = "usage_updated";
    admin::publish_event(usage);
    REQUIRE(stream.Pop("usage_updated").has_value());
    subscription["user_id"] = Json::Value{};
    stream.Send(subscription);
    REQUIRE(stream.Pop("subscribed").has_value());
    admin::publish_event(event_for("54321"));
    REQUIRE(stream.Pop("message_created").has_value());
    subscription["user_id"] = 12345;
    stream.Send(subscription);
    REQUIRE(stream.Pop("error").has_value());
    CHECK(test_support::request_admin("/api/admin/auth/logout", drogon::Post, {}, cookie)->statusCode() == drogon::k200OK);
    CHECK(stream.Closed());
    CHECK(!drogon::app().getPlugin<admin::Auth>()->Authenticated(cookie));
}

DROGON_TEST(LogStreamAppliesFiltersAndReplaysRetainedRecordsAfterReconnect) {
    auto const cookie{ test_support::login_admin() };
    Json::Value subscription{ Json::objectValue };
    subscription["type"] = "subscribe";
    subscription["level"] = "WARN";
    subscription["keyword"] = "stream-check";
    subscription["qq"] = "12345";
    std::string cursor{};
    {
        StreamClient stream{ "/api/admin/logs/ws", cookie };
        REQUIRE(stream.Pop("ready").has_value());
        stream.Send(subscription);
        REQUIRE(stream.Pop("logs").has_value());
        LOG_INFO << "stream-check (12345) excluded info";
        LOG_WARN << "stream-check (9123456) excluded QQ";
        LOG_WARN << "stream-check (12345) first";
        auto const batch{ stream.Pop("logs") };
        REQUIRE(batch.has_value());
        REQUIRE((*batch)["entries"].size() == 1);
        CHECK((*batch)["entries"][0]["message"].asString().find("first") != std::string::npos);
        cursor = (*batch)["cursor"].asString();
    }
    LOG_WARN << "stream-check (12345) during disconnect";
    subscription["after"] = cursor;
    StreamClient reconnected{ "/api/admin/logs/ws", cookie };
    REQUIRE(reconnected.Pop("ready").has_value());
    reconnected.Send(subscription);
    auto const replay{ reconnected.Pop("logs") };
    REQUIRE(replay.has_value());
    REQUIRE((*replay)["entries"].size() == 1);
    CHECK((*replay)["entries"][0]["message"].asString().find("during disconnect") != std::string::npos);
    subscription["level"] = "invalid";
    reconnected.Send(subscription);
    REQUIRE(reconnected.Pop("error").has_value());
    test_support::request_admin("/api/admin/auth/logout", drogon::Post, {}, cookie);
    CHECK(reconnected.Closed());
}

DROGON_TEST(WebSocketUpgradeRejectsCrossOriginCookies) {
    auto request{ drogon::HttpRequest::newHttpRequest() };
    request->setPath("/api/admin/ws");
    request->setPassThrough(true);
    request->addHeader("Connection", "Upgrade");
    request->addHeader("Upgrade", "websocket");
    request->addHeader("Sec-WebSocket-Version", "13");
    request->addHeader("Sec-WebSocket-Key", "dGhlIHNhbXBsZSBub25jZQ==");
    request->addHeader("Origin", "https://another.example");
    request->addCookie(admin::ADMIN_SESSION_COOKIE, test_support::login_admin());
    auto const client{ drogon::HttpClient::newHttpClient(test_support::SERVER_URL) };
    auto const [result, response]{ client->sendRequest(request, 5.0) };
    REQUIRE(result == drogon::ReqResult::Ok);
    CHECK(response->statusCode() == drogon::k403Forbidden);
    REQUIRE(response->getJsonObject() != nullptr);
}

auto main(int argc, char** argv) -> int {
    test_support::load_config();
    return test_support::run_tests_with_app(argc, argv);
}
