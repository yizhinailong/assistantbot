/**
 * @file test_admin_tooling.cpp
 * @brief Verify authenticated custom tool revisions and harmless Lua test execution.
 */
#include "admin_support.hpp"
#include "common/json.hpp"

DROGON_TEST(CustomToolApiUsesRevisionsAndMocksHostEffects) {
    CHECK(test_support::request_admin("/api/admin/tools/lua", drogon::Get, {}, "")->statusCode() == drogon::k401Unauthorized);
    CHECK(test_support::request_admin("/api/admin/tools/lua-test", drogon::Post, {}, "")->statusCode() == drogon::k401Unauthorized);
    CHECK(test_support::request_admin("/api/admin/tools/builtin")->getJsonObject()->operator[]("tools").size() >= 20);
    auto const doc{ common::parse_json(R"({"description":"Echo","parameters":{"type":"object","properties":{"text":{"type":"string"}},"required":["text"]},"script":"function run(args) local r=http.post('http://127.0.0.1:1',args.text); bot.send_message(r.body); bot.schedule(0,'mock',false); return r.body end"})") };
    Json::Value body{};
    body["value"] = doc;
    body["revision"] = "0";
    auto response{ test_support::request_admin("/api/admin/tools/lua/echo", drogon::Put, body) };
    REQUIRE(response->statusCode() == drogon::k200OK);
    CHECK((*response->getJsonObject())["revision"] == "1");
    CHECK(test_support::request_admin("/api/admin/tools/lua/echo", drogon::Put, body)->statusCode() == drogon::k409Conflict);
    body["name"] = "echo";
    body["arguments"]["text"] = "hello";
    response = test_support::request_admin("/api/admin/tools/lua-test", drogon::Post, body);
    REQUIRE(response->statusCode() == drogon::k200OK);
    CHECK((*response->getJsonObject())["result"] == "hello");
    CHECK((*response->getJsonObject())["operations"].size() == 3);
    CHECK(test_support::request_admin("/api/admin/tools/lua/echo?revision=1", drogon::Delete)->statusCode() == drogon::k200OK);
    CHECK(test_support::request_admin("/api/admin/tools/lua/echo", drogon::Put, body)->statusCode() == drogon::k200OK);
    CHECK(test_support::request_admin("/api/admin/mcp/missing/test", drogon::Post)->statusCode() == drogon::k404NotFound);
}

auto main(int argc, char** argv) -> int {
    test_support::load_config();
    return test_support::run_tests_with_app(argc, argv);
}
