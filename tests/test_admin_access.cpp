/**
 * @file test_admin_access.cpp
 * @brief Check authenticated access administration and typed friend list responses.
 */
#include <atomic>
#include <future>
#include <stdexcept>
#include <string>
#include <vector>

#include <drogon/drogon.h>
#include <drogon/drogon_test.h>

#include "controllers/onebot/assistant_bot.hpp"
#include "storage/database.hpp"
#include "support.hpp"
#include "workflow/access_control.hpp"

namespace {

    std::atomic_bool g_fail_friends{}; ///< Force the fake OneBot action to fail.

    /**
     * @brief Serve a typed OneBot friend list, including an ID beyond JavaScript precision.
     */
    auto serve_friends() -> void {
        drogon::app().registerHandler(
            "/napcat/get_friend_list",
            [](drogon::HttpRequestPtr) -> drogon::Task<drogon::HttpResponsePtr> {
                if (g_fail_friends) {
                    co_return drogon::HttpResponse::newHttpResponse(drogon::k503ServiceUnavailable, drogon::CT_TEXT_PLAIN);
                }
                Json::Value body{ Json::objectValue };
                body["status"] = "ok";
                body["retcode"] = 0;
                body["data"] = Json::Value{ Json::arrayValue };
                for (auto const qq : { 12345LL, 54321LL, 9007199254740993LL }) {
                    Json::Value friend_info{ Json::objectValue };
                    friend_info["user_id"] = Json::Int64{ qq };
                    friend_info["nickname"] = "测试好友";
                    friend_info["remark"] = "好友备注";
                    body["data"].append(friend_info);
                }
                co_return drogon::HttpResponse::newHttpJsonResponse(body);
            },
            { drogon::Post }
        );
    }

    /**
     * @brief Make a one-field request body.
     * @param key Field name.
     * @param value Field value.
     * @return JSON object.
     */
    auto field(char const* key, Json::Value const& value) -> Json::Value {
        Json::Value body{ Json::objectValue };
        body[key] = value;
        return body;
    }

    /**
     * @brief Send an administrative request with optional authentication.
     * @param path Absolute URL path.
     * @param method HTTP method.
     * @param body Optional JSON body.
     * @param authenticated Whether to sign in first.
     * @return HTTP response.
     * @throws std::runtime_error If sending fails.
     */
    auto request_admin(
        std::string const& path,
        drogon::HttpMethod method = drogon::Get,
        Json::Value const& body = Json::Value{},
        bool authenticated = true
    ) -> drogon::HttpResponsePtr {
        auto request{ body.isNull() ? drogon::HttpRequest::newHttpRequest() : drogon::HttpRequest::newHttpJsonRequest(body) };
        request->setPath(path);
        request->setMethod(method);
        if (authenticated) {
            request->addCookie("assistantbot_admin_session", test_support::login_admin());
        }
        auto const client{ drogon::HttpClient::newHttpClient(test_support::SERVER_URL) };
        auto const [result, response]{ client->sendRequest(request, 5.0) };
        if (result != drogon::ReqResult::Ok) {
            throw std::runtime_error{ "administrative request failed" };
        }
        return response;
    }

    /**
     * @brief Read JSON after checking the expected HTTP status.
     * @param response Response to inspect.
     * @return JSON body.
     * @throws std::runtime_error If the request failed or did not return JSON.
     */
    auto success(drogon::HttpResponsePtr const& response) -> Json::Value {
        if (response->statusCode() != drogon::k200OK || response->getJsonObject() == nullptr) {
            throw std::runtime_error{ "expected successful JSON response" };
        }
        return *response->getJsonObject();
    }

} // namespace

DROGON_TEST(AccessRoutesRequireAuthentication) {
    for (auto const* path : { "/api/admin/access", "/api/admin/allowlist", "/api/admin/blacklist", "/api/admin/friends" }) {
        CHECK(request_admin(path, drogon::Get, {}, false)->statusCode() == drogon::k401Unauthorized);
    }
    CHECK(request_admin("/api/admin/access", drogon::Put, field("mode", "allowlist"), false)->statusCode() == drogon::k401Unauthorized);
    for (auto const* path : { "/api/admin/allowlist", "/api/admin/blacklist" }) {
        CHECK(request_admin(path, drogon::Post, field("qq", "12345"), false)->statusCode() == drogon::k401Unauthorized);
        CHECK(request_admin(std::string{ path } + "/12345", drogon::Delete, {}, false)->statusCode() == drogon::k401Unauthorized);
    }
}

DROGON_TEST(AccessRoundTripAndFriendAnnotations) {
    CHECK(success(request_admin("/api/admin/access"))["mode"].asString() == "all_friends");
    CHECK(success(request_admin("/api/admin/allowlist"))["qqs"].empty());
    CHECK(success(request_admin("/api/admin/blacklist"))["qqs"].empty());
    CHECK(success(request_admin("/api/admin/access", drogon::Put, field("mode", "allowlist")))["mode"].asString() == "allowlist");
    for (auto const* path : { "/api/admin/allowlist", "/api/admin/blacklist" }) {
        auto const first{ success(request_admin(path, drogon::Post, field("qq", "0012345"))) };
        REQUIRE(first["qqs"].size() == 1);
        CHECK(first["qqs"][0].asString() == "12345");
        CHECK(success(request_admin(path, drogon::Post, field("qq", "12345"))) == first);
        CHECK(success(request_admin(path)) == first);
    }
    auto friends{ success(request_admin("/api/admin/friends"))["friends"] };
    REQUIRE(friends.size() == 3);
    CHECK(friends[0]["nickname"].asString() == "测试好友");
    CHECK(friends[0]["remark"].asString() == "好友备注");
    CHECK(friends[0]["in_allowlist"].asBool());
    CHECK(friends[0]["in_blacklist"].asBool());
    CHECK(!friends[0]["allowed"].asBool());
    CHECK(!friends[1]["allowed"].asBool());
    CHECK(friends[2]["user_id"].asString() == "9007199254740993");
    success(request_admin("/api/admin/blacklist/12345", drogon::Delete));
    friends = success(request_admin("/api/admin/friends"))["friends"];
    CHECK(friends[0]["allowed"].asBool());
    CHECK(!friends[0]["in_blacklist"].asBool());
    success(request_admin("/api/admin/allowlist", drogon::Post, field("qq", "9007199254740993")));
    success(request_admin("/api/admin/access", drogon::Put, field("mode", "all_friends")));
    success(request_admin("/api/admin/blacklist", drogon::Post, field("qq", "12345")));
    friends = success(request_admin("/api/admin/friends"))["friends"];
    CHECK(!friends[0]["allowed"].asBool());
    CHECK(friends[1]["allowed"].asBool());
    auto const saved{ drogon::app().getPlugin<controllers::AssistantBot>()->Access()->Current() };
    CHECK(saved->allowlist.contains(9007199254740993LL));
    for (auto const* path : { "/api/admin/allowlist", "/api/admin/blacklist" }) {
        success(request_admin(std::string{ path } + "/12345", drogon::Delete));
        success(request_admin(std::string{ path } + "/12345", drogon::Delete));
    }
    success(request_admin("/api/admin/allowlist/9007199254740993", drogon::Delete));
}

DROGON_TEST(AccessRejectsInvalidModeAndQqWithoutChangingPolicy) {
    auto const before{ drogon::app().getPlugin<controllers::AssistantBot>()->Access()->Current() };
    for (auto const& body : { Json::Value{}, Json::Value{ Json::arrayValue }, Json::Value{ Json::objectValue }, field("mode", "everyone"), field("mode", true), field("qq", "12345") }) {
        auto const response{ request_admin("/api/admin/access", drogon::Put, body) };
        CHECK(response->statusCode() == drogon::k400BadRequest);
        REQUIRE(response->getJsonObject() != nullptr);
        CHECK((*response->getJsonObject())["error"].isString());
    }
    std::vector<Json::Value> const invalid{
        Json::Value{},
        Json::Value{ Json::arrayValue },
        Json::Value{ Json::objectValue },
        field("qq", ""),
        field("qq", "0"),
        field("qq", "-1"),
        field("qq", "+1"),
        field("qq", "1.2"),
        field("qq", "1e5"),
        field("qq", "123x"),
        field("qq", " 123"),
        field("qq", "9223372036854775808"),
        field("qq", 12345),
        field("qq", 1.0),
        field("qq", true),
        field("qq", Json::Value{ Json::arrayValue })
    };
    for (auto const* path : { "/api/admin/allowlist", "/api/admin/blacklist" }) {
        for (auto const& body : invalid) {
            CHECK(request_admin(path, drogon::Post, body)->statusCode() == drogon::k400BadRequest);
        }
        for (auto const* qq : { "0", "-1", "1.2", "123x", "9223372036854775808" }) {
            CHECK(request_admin(std::string{ path } + "/" + qq, drogon::Delete)->statusCode() == drogon::k400BadRequest);
        }
    }
    CHECK(drogon::app().getPlugin<controllers::AssistantBot>()->Access()->Current() == before);
}

DROGON_TEST(FriendFailureDoesNotPreventEditingLists) {
    g_fail_friends = true;
    auto const failed{ request_admin("/api/admin/friends") };
    g_fail_friends = false;
    CHECK(failed->statusCode() == drogon::k502BadGateway);
    REQUIRE(failed->getJsonObject() != nullptr);
    CHECK((*failed->getJsonObject())["error"].isString());
    auto const saved{ success(request_admin("/api/admin/blacklist", drogon::Post, field("qq", "99999"))) };
    CHECK(saved["qqs"][0].asString() == "99999");
    success(request_admin("/api/admin/blacklist/99999", drogon::Delete));
}

DROGON_TEST(AccessApiReportsSaveFailuresAndConflicts) {
    auto const db{ drogon::app().getPlugin<storage::Database>()->Client() };
    auto const access{ drogon::app().getPlugin<controllers::AssistantBot>()->Access() };
    auto const before{ access->Current() };
    db->execSqlSync("PRAGMA query_only = ON");
    auto const failed{ request_admin("/api/admin/blacklist", drogon::Post, field("qq", "88888")) };
    db->execSqlSync("PRAGMA query_only = OFF");
    CHECK(failed->statusCode() == drogon::k500InternalServerError);
    CHECK(access->Current() == before);

    std::promise<void> slow_done{};
    db->execSqlAsync(
        "WITH RECURSIVE n(x) AS (SELECT 1 UNION ALL SELECT x + 1 FROM n WHERE x < 5000000) SELECT count(*) FROM n",
        [&slow_done](drogon::orm::Result const&) { slow_done.set_value(); },
        [&slow_done](drogon::orm::DrogonDbException const&) { slow_done.set_value(); }
    );
    std::promise<void> saved{};
    drogon::async_run([access, &saved]() -> drogon::Task<> {
        co_await access->SetListed(workflow::AccessList::Blacklist, 88888, true);
        saved.set_value();
    });
    auto const conflict{ request_admin("/api/admin/access", drogon::Put, field("mode", "allowlist")) };
    CHECK(conflict->statusCode() == drogon::k409Conflict);
    CHECK(access->Current() == before);
    saved.get_future().wait();
    slow_done.get_future().wait();
    success(request_admin("/api/admin/blacklist/88888", drogon::Delete));
}

auto main(int argc, char** argv) -> int {
    test_support::load_config();
    serve_friends();
    return test_support::run_tests_with_app(argc, argv);
}
