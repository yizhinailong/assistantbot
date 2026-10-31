/**
 * @file test_admin_auth.cpp
 * @brief Check administrative authentication, cookie sessions, and protected routes.
 */
#include <future>
#include <stdexcept>
#include <string>

#include <drogon/WebSocketClient.h>
#include <drogon/WebSocketController.h>
#include <drogon/drogon.h>
#include <drogon/drogon_test.h>

#include "admin/auth.hpp"
#include "support.hpp"

namespace {

    /**
     * @brief Send an administrative request without retaining cookies between calls.
     * @param path Absolute request path.
     * @param method HTTP method.
     * @param body Optional JSON body.
     * @param session Optional opaque cookie value.
     * @return HTTP response.
     * @throws std::runtime_error If the request fails.
     */
    auto auth_request(
        char const* path,
        drogon::HttpMethod method = drogon::Get,
        Json::Value const& body = {},
        std::string const& session = {}
    ) -> drogon::HttpResponsePtr {
        auto request{ body.isNull() ? drogon::HttpRequest::newHttpRequest() : drogon::HttpRequest::newHttpJsonRequest(body) };
        request->setMethod(method);
        request->setPath(path);
        if (!session.empty()) {
            request->addCookie(admin::ADMIN_SESSION_COOKIE, session);
        }
        auto const client{ drogon::HttpClient::newHttpClient(test_support::SERVER_URL) };
        auto const [result, response]{ client->sendRequest(request, 5.0) };
        if (result != drogon::ReqResult::Ok) {
            throw std::runtime_error{ "authentication request failed" };
        }
        return response;
    }

} // namespace

DROGON_TEST(AuthStatusAndHealthArePublic) {
    auto const response{ auth_request("/api/admin/auth/status") };
    REQUIRE(response->statusCode() == drogon::k200OK);
    REQUIRE(response->getJsonObject() != nullptr);
    CHECK(!(*response->getJsonObject())["authenticated"].asBool());
    CHECK(response->getHeader("Cache-Control") == "no-store");
    CHECK(auth_request("/api/admin/health")->statusCode() == drogon::k200OK);
    CHECK(auth_request("/api/admin/auth/status", drogon::Get, {}, "forged-session")->statusCode() == drogon::k200OK);
}

DROGON_TEST(AuthRejectsMissingAndIncorrectTokens) {
    for (auto const& invalid : { Json::Value{}, Json::Value{ Json::arrayValue }, Json::Value{ Json::objectValue } }) {
        CHECK(auth_request("/api/admin/auth/login", drogon::Post, invalid)->statusCode() == drogon::k400BadRequest);
    }
    Json::Value body{ Json::objectValue };
    for (auto const& invalid : { Json::Value{ true }, Json::Value{ "" }, Json::Value{ std::string(4097, 'x') } }) {
        body["token"] = invalid;
        CHECK(auth_request("/api/admin/auth/login", drogon::Post, body)->statusCode() == drogon::k400BadRequest);
    }
    body["token"] = "incorrect-token";
    auto const response{ auth_request("/api/admin/auth/login", drogon::Post, body) };
    CHECK(response->statusCode() == drogon::k401Unauthorized);
    CHECK(response->cookies().empty());
    CHECK(response->body().find("incorrect-token") == std::string::npos);
    CHECK(response->body().find(test_support::ADMIN_TOKEN) == std::string::npos);
}

DROGON_TEST(AuthLoginRotatesAndLogoutRevokesSession) {
    Json::Value body{ Json::objectValue };
    body["token"] = test_support::ADMIN_TOKEN;
    auto const first{ auth_request("/api/admin/auth/login", drogon::Post, body) };
    REQUIRE(first->statusCode() == drogon::k200OK);
    auto const cookie{ first->getCookie(admin::ADMIN_SESSION_COOKIE) };
    auto const session{ cookie.value() };
    REQUIRE(!session.empty());
    CHECK(session != test_support::ADMIN_TOKEN);
    auto const wire{ cookie.cookieString() };
    CHECK(wire.find("HttpOnly") != std::string::npos);
    CHECK(wire.find("SameSite=Strict") != std::string::npos);
    CHECK(wire.find("Path=/api/admin") != std::string::npos);
    CHECK(first->body().find(session) == std::string::npos);
    CHECK(first->body().find(test_support::ADMIN_TOKEN) == std::string::npos);
    auto const status{ auth_request("/api/admin/auth/status", drogon::Get, {}, session) };
    REQUIRE(status->getJsonObject() != nullptr);
    CHECK((*status->getJsonObject())["authenticated"].asBool());
    CHECK(auth_request("/api/admin/bot-status", drogon::Get, {}, session)->statusCode() == drogon::k200OK);

    auto const second{ auth_request("/api/admin/auth/login", drogon::Post, body, session) };
    REQUIRE(second->statusCode() == drogon::k200OK);
    auto const replacement{ second->getCookie(admin::ADMIN_SESSION_COOKIE).value() };
    REQUIRE(!replacement.empty());
    CHECK(replacement != session);
    CHECK(auth_request("/api/admin/bot-status", drogon::Get, {}, session)->statusCode() == drogon::k401Unauthorized);
    auto const logout{ auth_request("/api/admin/auth/logout", drogon::Post, {}, replacement) };
    REQUIRE(logout->statusCode() == drogon::k200OK);
    CHECK(logout->getCookie(admin::ADMIN_SESSION_COOKIE).value().empty());
    CHECK(logout->getCookie(admin::ADMIN_SESSION_COOKIE).cookieString().find("Max-Age=0") != std::string::npos);
    CHECK(auth_request("/api/admin/bot-status", drogon::Get, {}, replacement)->statusCode() == drogon::k401Unauthorized);
}

DROGON_TEST(AuthProtectsUnknownRoutesAndWebSocketUpgrades) {
    for (auto const* path : { "/api/admin", "/api/admin/unknown", "/api/admin/ws", "/api/admin/logs/ws", "/API/ADMIN/auth/logout", "/Api/Admin/bot-status" }) {
        CHECK(auth_request(path)->statusCode() == drogon::k401Unauthorized);
    }
    CHECK(auth_request("/api/admin/auth/logout", drogon::Post)->statusCode() == drogon::k401Unauthorized);
    auto request{ drogon::HttpRequest::newHttpRequest() };
    request->setPath("/api/admin/ws");
    // Preserve Connection: Upgrade; HttpClient otherwise replaces it with Keep-Alive.
    request->setPassThrough(true);
    request->addHeader("Connection", "Upgrade");
    request->addHeader("Upgrade", "websocket");
    request->addHeader("Sec-WebSocket-Version", "13");
    request->addHeader("Sec-WebSocket-Key", "dGhlIHNhbXBsZSBub25jZQ==");
    auto const client{ drogon::HttpClient::newHttpClient(test_support::SERVER_URL) };
    auto const [result, response]{ client->sendRequest(request, 5.0) };
    REQUIRE(result == drogon::ReqResult::Ok);
    CHECK(response->statusCode() == drogon::k401Unauthorized);
    request->addCookie(admin::ADMIN_SESSION_COOKIE, test_support::login_admin());
    auto const authenticated_client{ drogon::WebSocketClient::newWebSocketClient("ws://127.0.0.1:18082") };
    auto const connected{ std::make_shared<std::promise<drogon::HttpResponsePtr>>() };
    auto ready{ connected->get_future() };
    authenticated_client->connectToServer(request, [connected](drogon::ReqResult result, drogon::HttpResponsePtr const& response, drogon::WebSocketClientPtr const&) {
        connected->set_value(result == drogon::ReqResult::Ok ? response : nullptr);
    });
    REQUIRE(ready.wait_for(std::chrono::seconds{ 5 }) == std::future_status::ready);
    auto const authenticated_response{ ready.get() };
    authenticated_client->stop();
    REQUIRE(authenticated_response != nullptr);
    CHECK(authenticated_response->statusCode() == drogon::k101SwitchingProtocols);
}

DROGON_TEST(AuthConfigurationRequiresEnvironmentSecret) {
    admin::Auth auth{};
    CHECK_THROWS_AS(auth.initAndStart(Json::Value{}), std::invalid_argument);
    Json::Value config{ Json::objectValue };
    config["admin_token_env"] = "";
    CHECK_THROWS_AS(auth.initAndStart(config), std::invalid_argument);
    config["admin_token_env"] = "ASSISTANTBOT_TEST_EMPTY_ADMIN_TOKEN";
    test_support::set_env("ASSISTANTBOT_TEST_EMPTY_ADMIN_TOKEN", "");
    CHECK_THROWS_AS(auth.initAndStart(config), std::invalid_argument);
    config["admin_token_env"] = "ASSISTANTBOT_TEST_UNSET_ADMIN_TOKEN";
    CHECK_THROWS_AS(auth.initAndStart(config), std::invalid_argument);
    CHECK(admin::constant_time_equal("same", "same"));
    CHECK(!admin::constant_time_equal("same", "sand"));
    CHECK(!admin::constant_time_equal("same", "same-longer"));
    CHECK(!admin::constant_time_equal(std::string{ "a\0b", 3 }, "a"));
}

auto main(int argc, char** argv) -> int {
    test_support::load_config();
    return test_support::run_tests_with_app(argc, argv);
}
