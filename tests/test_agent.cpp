/**
 * @file test_agent.cpp
 * @brief Exercise role decisions, bounded tool effects, scoped reminders, MCP selection, and memory injection.
 */
#include <atomic>

#include "admin_support.hpp"
#include "common/json.hpp"
#include "common/time.hpp"
#include "controllers/onebot/assistant_bot.hpp"
#include "storage/database.hpp"
#include "storage/feature_records.hpp"
#include "workflow/agent.hpp"

namespace {
    std::atomic_int model_calls{};  ///< Requests observed by the fake executor.
    std::atomic_bool saw_remote{};  ///< Whether an explicitly selected remote schema was offered.
    std::atomic_int remote_calls{}; ///< MCP effects observed by the fake service.
    std::atomic_bool saw_memory{};  ///< Whether a prompt contained stored profile data.

    /**
     * @brief Return one complete non-streaming fake assistant response.
     * @param model Reported model.
     * @param text Reply content.
     * @param tool Optional tool call name.
     * @param arguments Function arguments.
     * @param id Model call identifier.
     * @return Wire completion object.
     */
    auto completion(
        std::string model,
        std::string text,
        std::string tool = {},
        Json::Value arguments = {},
        std::string id = "call"
    ) -> Json::Value {
        Json::Value body{};
        body["id"] = "agent-test";
        body["object"] = "chat.completion";
        body["created"] = 1;
        body["model"] = model;
        Json::Value choice{};
        choice["index"] = 0;
        choice["message"]["role"] = "assistant";
        choice["message"]["content"] = text;
        choice["finish_reason"] = "stop";
        if (!tool.empty()) {
            Json::Value call{};
            call["id"] = id;
            call["type"] = "function";
            call["function"]["name"] = tool;
            call["function"]["arguments"] = common::json_text(arguments);
            choice["message"]["tool_calls"].append(call);
            choice["finish_reason"] = "tool_calls";
        }
        body["choices"].append(choice);
        body["usage"]["prompt_tokens"] = 2;
        body["usage"]["completion_tokens"] = 1;
        body["usage"]["total_tokens"] = 3;
        return body;
    }
} // namespace

DROGON_TEST(AgentToolsStayScopedAndRespectLimitsAndRevisions) {
    auto const db{ drogon::app().getPlugin<storage::Database>()->Client() };
    auto features{ std::make_shared<workflow::FeatureSettings>(db) };
    auto models{ std::make_shared<workflow::ModelRoles>(drogon_openai::OpenAI{ { .base_url = std::string{ test_support::SERVER_URL } + "/agent", .model = "agent", .max_retries = 0 } }, db, "agent") };
    auto memory{ std::make_shared<workflow::MemoryService>(db, models) };
    auto tasks{ drogon::app().getPlugin<controllers::AssistantBot>()->Tasks() };
    workflow::AgentRuntime agent{ features, models, memory, tasks, db, drogon_onebot::OneBot{ drogon_onebot::OneBotConfig{ .base_url = std::string{ test_support::SERVER_URL } + "/napcat" } } };
    auto tools{ features->Describe("tools")["values"] };
    tools["enabled"] = true;
    drogon::sync_wait(features->Update("tools", tools));
    auto execution{ features->Describe("execution")["values"] };
    execution["max_tool_rounds"] = 3;
    drogon::sync_wait(features->Update("execution", execution));
    auto lua{ common::parse_json(R"({"enabled":true,"description":"Echo","parameters":{"type":"object","properties":{"text":{"type":"string"}},"required":["text"]},"script":"function run(args) return args.text end"})") };
    lua["script"] = "function run(args) local r=http.post('" + std::string{ test_support::SERVER_URL } + "/lua/echo',args.text); bot.send_message('lua notice'); bot.schedule(" + std::to_string(common::unix_seconds() + 3600) + ",'lua reminder',false); return r.body end";
    drogon::sync_wait(storage::save_feature_record(db, "lua", "echo_tool", lua, 0));
    int sent{};
    auto send{ [&sent](drogon_onebot::Message, std::string) -> drogon::Task<Json::Value> {++sent;co_return common::parse_json(R"({"ok":true,"message_id":"1"})"); } };
    auto run{ [&](std::string prompt) {drogon_openai::ChatCompletionCreateParams params{};params.messages={{.role="user",.content=prompt}};return drogon::sync_wait(agent.Run({.user_id=55001},params,prompt,send)); } };
    auto result{ run("lua flow") };
    CHECK(result.completed);
    CHECK(result.text == "lua done");
    CHECK(sent == 1);
    result = run("terminal reply");
    CHECK(result.completed);
    CHECK(result.delivered);
    CHECK(result.text == "tool reply");
    CHECK(sent == 2);
    result = run("silent");
    CHECK(result.completed);
    CHECK(result.text.empty());
    CHECK(sent == 2);
    result = run("repeat effect");
    CHECK(result.completed);
    CHECK(sent == 3);
    result = run("round limit");
    CHECK(!result.completed);
    CHECK(sent == 3);
    tools["builtin"]["reply"]["enabled"] = false;
    drogon::sync_wait(features->Update("tools", tools));
    result = run("disabled tool");
    CHECK(result.text == "disabled rejected");
    CHECK(sent == 3);
    auto task{ drogon::sync_wait(tasks->Create({ .user_id = 55002 }, common::unix_seconds() + 3600, "other conversation", false)) };
    // The fake model uses the stored ID; the runtime must retain the conversation restriction.
    CHECK(!drogon::sync_wait(tasks->Cancel(std::stoll(task["id"].asString()), 55001)));
    CHECK(drogon::sync_wait(tasks->Cancel(std::stoll(task["id"].asString()), 55002)));
    for (auto const& own : drogon::sync_wait(tasks->List(55001))) {
        if (own["status"] == "pending") {
            CHECK(drogon::sync_wait(tasks->Cancel(std::stoll(own["id"].asString()), 55001)));
        }
    }
}

DROGON_TEST(AgentRolesAndMemoryAffectActualRequests) {
    auto const db{ drogon::app().getPlugin<storage::Database>()->Client() };
    auto features{ std::make_shared<workflow::FeatureSettings>(db) };
    auto models{ std::make_shared<workflow::ModelRoles>(drogon_openai::OpenAI{ { .base_url = std::string{ test_support::SERVER_URL } + "/agent", .model = "agent", .max_retries = 0 } }, db, "agent") };
    auto memory{ std::make_shared<workflow::MemoryService>(db, models) };
    workflow::AgentRuntime agent{ features, models, memory, drogon::app().getPlugin<controllers::AssistantBot>()->Tasks(), db, drogon_onebot::OneBot{ drogon_onebot::OneBotConfig{ .base_url = std::string{ test_support::SERVER_URL } + "/napcat" } } };
    auto roles{ features->Describe("models")["values"] };
    roles["decision"]["enabled"] = true;
    roles["decision"]["model"] = "decision-deny";
    drogon::sync_wait(features->Update("models", roles));
    int sends{};
    auto send{ [&](drogon_onebot::Message, std::string) -> drogon::Task<Json::Value> {++sends;co_return common::parse_json(R"({"ok":true})"); } };
    auto run{ [&](std::string prompt) {drogon_openai::ChatCompletionCreateParams params{};params.messages={{.role="user",.content=prompt}};return drogon::sync_wait(agent.Run({.user_id=55003},params,prompt,send)); } };
    auto calls{ model_calls.load() };
    auto result{ run("ignore me") };
    CHECK(result.completed);
    CHECK(result.text.empty());
    CHECK(model_calls.load() == calls + 1);
    roles["decision"]["enabled"] = false;
    roles["executor"]["enabled"] = true;
    roles["executor"]["model"] = "fail";
    roles["executor_fallback"]["enabled"] = true;
    roles["executor_fallback"]["model"] = "fallback";
    drogon::sync_wait(features->Update("models", roles));
    result = run("fallback please");
    CHECK(result.text == "fallback");
    roles["executor"]["enabled"] = false;
    roles["executor_fallback"]["enabled"] = false;
    roles["memory"]["enabled"] = true;
    roles["memory"]["model"] = "memory";
    drogon::sync_wait(features->Update("models", roles));
    auto settings{ features->Describe("memory")["values"] };
    settings["enabled"] = true;
    settings["maintenance_turns"] = 2;
    settings["affinity_enabled"] = true;
    drogon::sync_wait(features->Update("memory", settings));
    result = run("my favourite drink is tea");
    CHECK(result.completed);
    result = run("remember the tea preference");
    CHECK(result.completed);
    auto people{ drogon::sync_wait(storage::feature_records(db, "people", "55003")) };
    REQUIRE(people.size() == 1);
    CHECK(people[0]["value"]["affinity"].asInt() == 1);
    saw_memory = false;
    result = run("next message");
    CHECK(saw_memory.load());
    CHECK(sends == 0);
}

DROGON_TEST(AgentOffersOnlyAvailableCapabilitiesAndResolvesSourceImages) {
    auto const db{ drogon::app().getPlugin<storage::Database>()->Client() };
    auto features{ std::make_shared<workflow::FeatureSettings>(db) };
    auto roles{ workflow::default_feature_settings()["models"] };
    drogon::sync_wait(features->Update("models", roles));
    drogon::sync_wait(features->Update("memory", workflow::default_feature_settings()["memory"]));
    auto tools{ workflow::default_feature_settings()["tools"] };
    tools["enabled"] = true;
    drogon::sync_wait(features->Update("tools", tools));
    auto models{ std::make_shared<workflow::ModelRoles>(drogon_openai::OpenAI{ { .base_url = std::string{ test_support::SERVER_URL } + "/agent", .model = "agent", .max_retries = 0 } }, db, "agent") };
    auto memory{ std::make_shared<workflow::MemoryService>(db, models) };
    workflow::AgentRuntime agent{ features, models, memory, drogon::app().getPlugin<controllers::AssistantBot>()->Tasks(), db, drogon_onebot::OneBot{ { .base_url = std::string{ test_support::SERVER_URL } + "/napcat" } } };
    auto send{ [](drogon_onebot::Message, std::string) -> drogon::Task<Json::Value> {throw std::runtime_error{"unexpected send"};co_return Json::Value{}; } };
    drogon_openai::ChatCompletionCreateParams params{};
    params.messages = { { .role = "user", .content = "capability gates" } };
    auto result{ drogon::sync_wait(agent.Run({ .user_id = 55005 }, params, "capability gates", send)) };
    CHECK(result.text == "gates checked");
    roles["vision"]["enabled"] = true;
    roles["vision"]["model"] = "vision";
    drogon::sync_wait(features->Update("models", roles));
    params.messages = { { .role = "user", .content = "image reference" } };
    result = drogon::sync_wait(agent.Run({ .user_id = 55005, .message_id = 42, .image_urls = { "https://example.test/current.png" } }, params, "image reference", send));
    CHECK(result.completed);
    CHECK(result.text == "image checked");
}

DROGON_TEST(McpSchemasRequireSelectionAndDoNotLeakAcrossReplies) {
    auto const db{ drogon::app().getPlugin<storage::Database>()->Client() };
    auto features{ std::make_shared<workflow::FeatureSettings>(db) };
    drogon::sync_wait(features->Update("models", workflow::default_feature_settings()["models"]));
    auto tools{ features->Describe("tools")["values"] };
    tools["enabled"] = true;
    drogon::sync_wait(features->Update("tools", tools));
    auto execution{ features->Describe("execution")["values"] };
    execution["max_tool_rounds"] = 5;
    drogon::sync_wait(features->Update("execution", execution));
    Json::Value mcp{};
    auto server{ common::parse_json(R"({"name":"local","url":"","enabled":true})") };
    server["url"] = std::string{ test_support::SERVER_URL } + "/agent/mcp";
    mcp["servers"].append(server);
    drogon::sync_wait(features->Update("mcp", mcp));
    auto models{ std::make_shared<workflow::ModelRoles>(drogon_openai::OpenAI{ { .base_url = std::string{ test_support::SERVER_URL } + "/agent", .model = "agent", .max_retries = 0 } }, db, "agent") };
    auto memory{ std::make_shared<workflow::MemoryService>(db, models) };
    workflow::AgentRuntime agent{ features, models, memory, drogon::app().getPlugin<controllers::AssistantBot>()->Tasks(), db, drogon_onebot::OneBot{ { .base_url = std::string{ test_support::SERVER_URL } + "/napcat" } } };
    auto send{ [](drogon_onebot::Message, std::string) -> drogon::Task<Json::Value> {throw std::runtime_error{"unexpected send"};co_return Json::Value{}; } };
    for (int i{}; i < 2; ++i) {
        saw_remote = false;
        drogon_openai::ChatCompletionCreateParams params{};
        params.messages = { { .role = "user", .content = "MCP flow" } };
        auto result{ drogon::sync_wait(agent.Run({ .user_id = 55004 }, params, "MCP flow", send)) };
        CHECK(result.text == "remote done");
        CHECK(saw_remote.load());
    }
    CHECK(remote_calls.load() == 2);
}

auto main(int argc, char** argv) -> int {
    test_support::load_config();
    drogon::app().registerHandler("/agent/chat/completions", [](drogon::HttpRequestPtr req) -> drogon::Task<drogon::HttpResponsePtr> {
        ++model_calls;
        auto const body{ req->getJsonObject() };
        auto const model{ (*body)["model"].asString() };
        if (model == "fail") {
            auto response{ drogon::HttpResponse::newHttpResponse() };
            response->setStatusCode(drogon::k503ServiceUnavailable);
            co_return response;
        }
        if (model == "vision") {
            if ((*body)["messages"][0]["content"][1]["image_url"]["url"] != "https://example.test/current.png") {
                throw std::runtime_error{ "vision did not receive the selected source image" };
            }
            co_return drogon::HttpResponse::newHttpJsonResponse(completion(model, "source image"));
        }
        for (auto const& message : (*body)["messages"]) {
            if (message["role"] == "system" && message["content"].isString() && message["content"].asString().contains("likes tea")) {
                saw_memory = true;
            }
        }
        std::string prompt{};
        int calls{};
        for (auto const& message : (*body)["messages"]) {
            if (message["role"] == "user") {
                prompt = message["content"].asString();
            }
            if (message["role"] == "tool") {
                ++calls;
            }
        }
        Json::Value result{};
        if (prompt == "capability gates") {
            for (auto const& tool : (*body)["tools"]) {
                auto const name{ tool["function"]["name"].asString() };
                if (name == "reply_with_quote" || name == "deep_think" || name == "generate_image" || name == "ask_images" || name == "recall_memory" || name == "save_memory" || name == "delete_memory") {
                    throw std::runtime_error{ "unavailable capability was offered: " + name };
                }
            }
            result = completion(model, "gates checked");
        } else if (prompt == "image reference") {
            if (calls == 0) {
                auto const metadata{ (*body)["messages"][(*body)["messages"].size() - 2]["content"].asString() };
                if (!metadata.contains("42") || !metadata.contains("image_count")) {
                    throw std::runtime_error{ "source metadata is absent" };
                }
                result = completion(model, "", "ask_images", common::parse_json(R"({"question":"Describe","images":[0]})"));
            } else {
                if (!(*body)["messages"][(*body)["messages"].size() - 1]["content"].asString().contains("source image")) {
                    throw std::runtime_error{ "vision tool result is absent" };
                }
                result = completion(model, "image checked");
            }
        } else if (model == "decision-deny") {
            result = completion(model, R"({"reply":false})");
        } else if (model == "fallback") {
            result = completion(model, "fallback");
        } else if (model == "memory") {
            result = completion(model, R"({"profile":["likes tea"],"relationship":[],"recent_interactions":[],"memory":"likes tea","affinity_delta":1})");
        } else if (prompt == "MCP flow") {
            bool offered{};
            for (auto const& tool : (*body)["tools"]) {
                offered = offered || tool["function"]["name"] == "mcp_local_0";
            }
            if (calls < 2 && offered) {
                throw std::runtime_error{ "remote schema leaked before selection" };
            }
            if (calls == 0) {
                result = completion(model, "", "mcp_list_tools", common::parse_json(R"({"server":"local"})"), "discover");
            } else if (calls == 1) {
                result = completion(model, "", "mcp_enable_tools", common::parse_json(R"({"server":"local","names":["echo"]})"), "enable");
            } else if (calls == 2) {
                saw_remote = offered;
                result = completion(model, "", "mcp_local_0", common::parse_json(R"({"text":"hello"})"), "remote");
            } else {
                result = completion(model, "remote done");
            }
        } else if (prompt == "terminal reply") {
            result = completion(model, "", "reply", common::parse_json(R"({"content":"tool reply"})"));
        } else if (prompt == "silent") {
            result = completion(model, "", "no_reply", Json::Value{ Json::objectValue });
        } else if (prompt == "lua flow") {
            result = calls == 0 ? completion(model, "", "echo_tool", common::parse_json(R"({"text":"hello"})")) : completion(model, "lua done");
        } else if (prompt == "repeat effect") {
            result = completion(model, "", "send_face", common::parse_json(R"({"id":1})"), "same-effect");
        } else if (prompt == "round limit") {
            result = completion(model, "", "list_scheduled_tasks", Json::Value{ Json::objectValue }, "limit-" + std::to_string(calls));
        } else if (prompt == "disabled tool") {
            result = calls == 0 ? completion(model, "", "reply", common::parse_json(R"({"content":"must not send"})")) : completion(model, "disabled rejected");
        } else {
            result = completion(model, "ordinary");
        }
        co_return drogon::HttpResponse::newHttpJsonResponse(result);
    },
                                  { drogon::Post });
    drogon::app().registerHandler("/lua/echo", [](drogon::HttpRequestPtr req) -> drogon::Task<drogon::HttpResponsePtr> {auto response{drogon::HttpResponse::newHttpResponse()};response->setBody(std::string{req->body()});co_return response; }, { drogon::Post });
    drogon::app().registerHandler("/agent/mcp", [](drogon::HttpRequestPtr req) -> drogon::Task<drogon::HttpResponsePtr> {
        if (req->method() == drogon::Delete) {
            co_return drogon::HttpResponse::newHttpResponse();
        }
        auto const body{ req->getJsonObject() };
        if (!body) {
            co_return drogon::HttpResponse::newHttpResponse();
        }
        auto const method{ (*body)["method"].asString() };
        if (method == "notifications/initialized") {
            auto response{ drogon::HttpResponse::newHttpResponse() };
            response->setStatusCode(drogon::k202Accepted);
            co_return response;
        }
        Json::Value reply{};
        reply["jsonrpc"] = "2.0";
        reply["id"] = (*body)["id"];
        if (method == "initialize") {
            reply["result"]["protocolVersion"] = "2025-11-25";
        } else if (method == "tools/list") {
            reply["result"]["tools"].append(common::parse_json(R"({"name":"echo","description":"Echo","inputSchema":{"type":"object","properties":{"text":{"type":"string"}}}})"));
        } else {
            ++remote_calls;
            reply["result"]["structuredContent"] = (*body)["params"]["arguments"];
            reply["result"]["content"] = Json::Value{ Json::arrayValue };
        }
        auto response{ drogon::HttpResponse::newHttpJsonResponse(reply) };
        if (method == "initialize") {
            response->addHeader("Mcp-Session-Id", "agent-session");
        }
        co_return response;
    },
                                  { drogon::Post, drogon::Delete });
    return test_support::run_tests_with_app(argc, argv);
}
