/**
 * @file test_mcp.cpp
 * @brief Verify legacy handshakes, modern metadata and mirrored headers, SSE, and invalid discovery.
 */
#include "admin/http_helpers.hpp"
#include "admin_support.hpp"
#include "common/json.hpp"
#include "inbox.hpp"
#include "tools/mcp.hpp"

namespace {
    test_support::Inbox<Json::Value> observed{};

    /**
     * @brief Build one local service configuration.
     * @param name Fake endpoint suffix.
     * @param version Explicit supported protocol version.
     * @return Complete client configuration.
     */
    auto config(std::string name, std::string version) -> Json::Value {
        auto result{ common::parse_json(R"({"token_env":"","timeout":5})") };
        result["url"] = std::string{ test_support::SERVER_URL } + "/mcp/" + name;
        result["protocol_version"] = version;
        return result;
    }
} // namespace

DROGON_TEST(LegacyMcpInitializesPaginatesCallsAndCloses) {
    tools::McpSession session{ config("legacy", "2025-11-25") };
    auto list{ drogon::sync_wait(session.List()) };
    REQUIRE(list.size() == 2);
    Json::Value args{};
    args["text"] = "hello";
    auto result{ drogon::sync_wait(session.Call("echo", args)) };
    CHECK(result["structuredContent"]["text"] == "hello");
    CHECK(!result["isError"].asBool());
    drogon::sync_wait(session.Close());
    auto init{ observed.Pop() };
    REQUIRE(init);
    CHECK((*init)["method"] == "initialize");
    auto initialized{ observed.Pop() };
    REQUIRE(initialized);
    CHECK((*initialized)["session"] == "local-session");
    auto page1{ observed.Pop() };
    REQUIRE(page1);
    CHECK((*page1)["method"] == "tools/list");
    auto page2{ observed.Pop() };
    REQUIRE(page2);
    CHECK((*page2)["method"] == "tools/list");
    auto call{ observed.Pop() };
    REQUIRE(call);
    CHECK((*call)["method"] == "tools/call");
    auto closed{ observed.Pop() };
    REQUIRE(closed);
    CHECK((*closed)["method"] == "DELETE");
}

DROGON_TEST(ModernMcpUsesStatelessMetadataAndSafeMirroredHeaders) {
    tools::McpSession session{ config("modern", "2026-07-28") };
    auto list{ drogon::sync_wait(session.List()) };
    REQUIRE(list.size() == 1);
    Json::Value args{};
    args["text"] = " 中文 ";
    auto result{ drogon::sync_wait(session.Call("echo", args)) };
    CHECK(result["structuredContent"]["text"] == " 中文 ");
    auto discovery{ observed.Pop() };
    REQUIRE(discovery);
    CHECK((*discovery)["method"] == "tools/list");
    CHECK((*discovery)["rpc_method"] == "tools/list");
    CHECK((*discovery)["meta"]["io.modelcontextprotocol/protocolVersion"] == "2026-07-28");
    auto call{ observed.Pop() };
    REQUIRE(call);
    CHECK((*call)["rpc_method"] == "tools/call");
    CHECK((*call)["rpc_name"] == "echo");
    CHECK((*call)["parameter"] == "=?base64?IOS4reaWhyA=?=");
    CHECK((*call)["session"] == "");
    drogon::sync_wait(session.Close());
}

DROGON_TEST(McpRejectsRepeatedPaginationAndMismatchedResponses) {
    tools::McpSession repeated{ config("repeated", "2026-07-28") };
    CHECK_THROWS_AS(static_cast<void>(drogon::sync_wait(repeated.List())), std::runtime_error);
    tools::McpSession mismatch{ config("mismatch", "2026-07-28") };
    CHECK_THROWS_AS(static_cast<void>(drogon::sync_wait(mismatch.List())), std::runtime_error);
}

auto main(int argc, char** argv) -> int {
    test_support::load_config();
    for (auto const* name : { "legacy", "modern", "repeated", "mismatch" }) {
        drogon::app().registerHandler(std::string{ "/mcp/" } + name, [name](drogon::HttpRequestPtr req) -> drogon::Task<drogon::HttpResponsePtr> {
            auto const body{ req->getJsonObject() };
            if (!body && req->method() != drogon::Delete) {
                co_return admin::json_error("missing JSON request", drogon::k400BadRequest);
            }
            auto const method{ req->method() == drogon::Delete ? "DELETE" : body->get("method", "").asString() };
            if (std::string_view{ name } == "legacy" || std::string_view{ name } == "modern") {
                Json::Value event{};
                event["method"] = method;
                event["session"] = req->getHeader("mcp-session-id");
                event["rpc_method"] = req->getHeader("mcp-method");
                event["rpc_name"] = req->getHeader("mcp-name");
                event["parameter"] = req->getHeader("mcp-param-text");
                if (body) {
                    event["meta"] = (*body)["params"]["_meta"];
                }
                observed.Push(event);
            }
            if (method == "DELETE" || method == "notifications/initialized") {
                auto response{ drogon::HttpResponse::newHttpResponse() };
                response->setStatusCode(drogon::k202Accepted);
                co_return response;
            }
            Json::Value reply{};
            reply["jsonrpc"] = "2.0";
            reply["id"] = std::string_view{ name } == "mismatch" ? Json::Value{ 999 } : (*body)["id"];
            if (method == "initialize") {
                reply["result"]["protocolVersion"] = "2025-11-25";
                reply["result"]["capabilities"]["tools"] = Json::Value{ Json::objectValue };
            } else if (method == "tools/list") {
                auto tool{ common::parse_json(R"({"name":"echo","description":"Echo","inputSchema":{"type":"object","properties":{"text":{"type":"string","x-mcp-header":"Text"}}}})") };
                if (std::string_view{ name } == "legacy" && (*body)["params"].isMember("cursor")) {
                    tool["name"] = "second";
                }
                reply["result"]["tools"].append(tool);
                if (std::string_view{ name } == "legacy" && !(*body)["params"].isMember("cursor")) {
                    reply["result"]["nextCursor"] = "second";
                }
                if (std::string_view{ name } == "repeated") {
                    reply["result"]["nextCursor"] = "same";
                    reply["result"]["tools"] = Json::Value{ Json::arrayValue };
                }
                if (std::string_view{ name } == "modern") {
                    tool["name"] = "bad";
                    tool["inputSchema"]["x-mcp-header"] = "invalid-root";
                    reply["result"]["tools"].append(tool);
                }
            } else {
                reply["result"]["structuredContent"] = (*body)["params"]["arguments"];
                reply["result"]["content"] = Json::Value{ Json::arrayValue };
                reply["result"]["isError"] = false;
            }
            auto response{ drogon::HttpResponse::newHttpJsonResponse(reply) };
            if (method == "initialize") {
                response->addHeader("Mcp-Session-Id", "local-session");
            }
            if (std::string_view{ name } == "modern") {
                response = drogon::HttpResponse::newHttpResponse();
                response->setContentTypeString("text/event-stream");
                response->setBody("event: message\ndata: " + common::json_text(reply) + "\n\n");
            }
            co_return response;
        },
                                      { drogon::Post, drogon::Delete });
    }
    return test_support::run_tests_with_app(argc, argv);
}
