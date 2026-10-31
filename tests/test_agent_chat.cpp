/**
 * @file test_agent_chat.cpp
 * @brief Verify terminal tool messages obey text splitting, quote scope, and append-only delivery records.
 */
#include "admin_support.hpp"
#include "common/json.hpp"
#include "controllers/onebot/assistant_bot.hpp"
#include "inbox.hpp"
#include "storage/database.hpp"
#include "storage/messages.hpp"
#include "workflow/bot_settings.hpp"
#include "workflow/feature_settings.hpp"
#include "workflow/private_chat.hpp"

namespace {
    test_support::Inbox<Json::Value> outgoing{};
}

DROGON_TEST(ToolRepliesSplitUtf8AndQuoteOnlyTheFirstPart) {
    auto* bot{ drogon::app().getPlugin<controllers::AssistantBot>() };
    auto tools{ bot->Features()->Describe("tools")["values"] };
    tools["enabled"] = true;
    drogon::sync_wait(bot->Features()->Update("tools", tools));
    auto roles{ bot->Features()->Describe("models")["values"] };
    roles["executor"]["enabled"] = true;
    roles["executor"]["base_url"] = std::string{ test_support::SERVER_URL } + "/tool-model";
    drogon::sync_wait(bot->Features()->Update("models", roles));
    Json::Value settings{};
    settings["max_reply_chars"] = 3;
    drogon::sync_wait(bot->Settings()->Update(settings));
    auto chat{ bot->Chat() };
    drogon::sync_wait(chat->Chat({ .user_id = 77001, .message_id = 42 }, "quote"));
    std::string joined{};
    for (int i{}; i < 3; ++i) {
        auto sent{ outgoing.Pop() };
        REQUIRE(sent);
        auto const& message{ (*sent)["message"] };
        CHECK(message.size() == (i == 0 ? 2 : 1));
        if (i == 0) {
            CHECK(message[0]["type"] == "reply");
            CHECK(message[0]["data"]["id"] == "42");
        }
        joined += message[message.size() - 1]["data"]["text"].asString();
    }
    CHECK(joined == "一二三四五六七八");
    auto detail{ chat->Session(77001) };
    REQUIRE(detail);
    REQUIRE(detail->history.size() == 1);
    CHECK(detail->history[0].assistant == joined);
    auto records{ drogon::sync_wait(storage::message_page(drogon::app().getPlugin<storage::Database>()->Client(), 77001, 20)) };
    CHECK(records["messages"].size() == 3);
    for (auto const& message : records["messages"]) {
        CHECK(message["status"] == "sent");
    }
}

auto main(int argc, char** argv) -> int {
    test_support::load_config();
    drogon::app().registerHandler("/tool-model/chat/completions", [](drogon::HttpRequestPtr) -> drogon::Task<drogon::HttpResponsePtr> {
        auto body{ common::parse_json(R"({"id":"tool-reply","object":"chat.completion","created":1,"model":"tools","choices":[{"index":0,"finish_reason":"tool_calls","message":{"role":"assistant","content":null,"tool_calls":[{"id":"reply-once","type":"function","function":{"name":"reply_with_quote","arguments":"{\"content\":\"一二三四五六七八\",\"message_id\":\"42\"}"}}]}}]})") };
        co_return drogon::HttpResponse::newHttpJsonResponse(body);
    },
                                  { drogon::Post });
    drogon::app().registerHandler("/napcat/send_private_msg", [](drogon::HttpRequestPtr req) -> drogon::Task<drogon::HttpResponsePtr> {outgoing.Push(*req->getJsonObject());co_return drogon::HttpResponse::newHttpJsonResponse(common::parse_json(R"({"status":"ok","retcode":0,"data":{"message_id":1}})")); }, { drogon::Post });
    return test_support::run_tests_with_app(argc, argv);
}
