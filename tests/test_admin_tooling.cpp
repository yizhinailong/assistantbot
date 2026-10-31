/**
 * @file test_admin_tooling.cpp
 * @brief Verify authenticated custom tool revisions and harmless Lua test execution.
 */
#include "admin_support.hpp"
#include "common/json.hpp"
#include "controllers/onebot/assistant_bot.hpp"
#include "workflow/feature_settings.hpp"

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
    CHECK(test_support::request_admin("/api/admin/tools/lua/echo?revision=2", drogon::Delete)->statusCode() == drogon::k409Conflict);
    CHECK(test_support::request_admin("/api/admin/tools/lua/echo?revision=1", drogon::Delete)->statusCode() == drogon::k200OK);
    CHECK(test_support::request_admin("/api/admin/tools/lua/echo", drogon::Put, body)->statusCode() == drogon::k200OK);
    CHECK(test_support::request_admin("/api/admin/mcp/missing/test", drogon::Post)->statusCode() == drogon::k404NotFound);
}

DROGON_TEST(McpProbeUsesSavedConfigAndRequiresAuthentication) {
    CHECK(test_support::request_admin("/api/admin/mcp/probe/test", drogon::Post, {}, "")->statusCode() == drogon::k401Unauthorized);
    Json::Value config{};
    Json::Value server{};
    server["name"] = "probe";
    server["url"] = std::string{ test_support::SERVER_URL } + "/probe";
    config["servers"].append(server);
    drogon::sync_wait(drogon::app().getPlugin<controllers::AssistantBot>()->Features()->Update("mcp", config));
    auto response{ test_support::request_admin("/api/admin/mcp/probe/test", drogon::Post) };
    REQUIRE(response->statusCode() == drogon::k200OK);
    CHECK((*response->getJsonObject())["tools"].size() == 1);
}

auto main(int argc, char** argv) -> int {
    test_support::load_config();
    drogon::app().registerHandler("/probe", [](drogon::HttpRequestPtr req) -> drogon::Task<drogon::HttpResponsePtr> {
        auto const body{ req->getJsonObject() };
        if (!body) {
            co_return drogon::HttpResponse::newHttpResponse();
        }
        if ((*body)["method"] == "notifications/initialized") {
            auto response{ drogon::HttpResponse::newHttpResponse() };
            response->setStatusCode(drogon::k202Accepted);
            co_return response;
        }
        Json::Value reply{};
        reply["jsonrpc"] = "2.0";
        reply["id"] = (*body)["id"];
        if ((*body)["method"] == "initialize") {
            reply["result"]["protocolVersion"] = "2025-11-25";
        } else {
            reply["result"]["tools"].append(common::parse_json(R"({"name":"echo","inputSchema":{"type":"object"}})"));
        }
        co_return drogon::HttpResponse::newHttpJsonResponse(reply);
    },
                                  { drogon::Post });
    return test_support::run_tests_with_app(argc, argv);
}
