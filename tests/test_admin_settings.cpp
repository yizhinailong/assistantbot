/**
 * @file test_admin_settings.cpp
 * @brief Verify reply setting validation, defaults, persistence, and HTTP save conflicts.
 */
#include <future>

#include <drogon/drogon_test.h>

#include "admin_support.hpp"
#include "controllers/onebot/assistant_bot.hpp"
#include "storage/database.hpp"
#include "storage/settings.hpp"
#include "workflow/bot_settings.hpp"

DROGON_TEST(ReplySettingsRoundTripAndRestoreDefaultsWithoutLeakingSecrets) {
    CHECK(test_support::request_admin("/api/admin/bot-config", drogon::Get, {}, "")->statusCode() == drogon::k401Unauthorized);
    CHECK(test_support::request_admin("/api/admin/bot-config", drogon::Put, Json::Value{ Json::objectValue }, "")->statusCode() == drogon::k401Unauthorized);
    auto const before{ test_support::success(test_support::request_admin("/api/admin/bot-config")) };
    CHECK(before["values"]["model"] == "fake-model");
    CHECK(before["overrides"].empty());
    CHECK(before["default_system_prompt"] == workflow::DEFAULT_SYSTEM_PROMPT);
    Json::Value overrides{ Json::objectValue };
    overrides["system_prompt"] = "line one\nline two";
    overrides["history_turns"] = 0;
    overrides["idle_reset_minutes"] = 2;
    overrides["request_timeout"] = 0.5;
    overrides["max_tokens"] = 32;
    overrides["max_reply_chars"] = 20;
    overrides["model"] = "alternate-model";
    auto const updated{ test_support::success(test_support::request_admin("/api/admin/bot-config", drogon::Put, overrides)) };
    CHECK(updated["values"] == overrides);
    CHECK(updated["overrides"] == overrides);
    CHECK(updated["defaults"] == before["defaults"]);
    auto const db{ drogon::app().getPlugin<storage::Database>()->Client() };
    CHECK(storage::load_setting(*db, workflow::BOT_SETTINGS_NAME) == overrides);
    CHECK(test_support::success(test_support::request_admin("/api/admin/bot-config")) == updated);
    overrides["max_tokens"] = Json::Value{};
    auto const unlimited{ test_support::success(test_support::request_admin("/api/admin/bot-config", drogon::Put, overrides)) };
    CHECK(unlimited["values"]["max_tokens"].isNull());
    auto const restored{ test_support::success(test_support::request_admin("/api/admin/bot-config", drogon::Put, Json::Value{ Json::objectValue })) };
    CHECK(restored == before);
    auto const text{ Json::writeString(Json::StreamWriterBuilder{}, restored) };
    CHECK(text.find("api_key") == std::string::npos);
    CHECK(text.find(test_support::ADMIN_TOKEN) == std::string::npos);
}

DROGON_TEST(ReplySettingsReturnExactValidationErrorsAndKeepTheSnapshot) {
    auto const settings{ drogon::app().getPlugin<controllers::AssistantBot>()->Settings() };
    auto const before{ settings->Current() };
    auto const bad{ test_support::request_admin("/api/admin/bot-config", drogon::Put, test_support::field("history_turns", -1)) };
    CHECK(bad->statusCode() == drogon::k400BadRequest);
    REQUIRE(bad->getJsonObject() != nullptr);
    CHECK((*bad->getJsonObject())["error"] == "controllers::AssistantBot: history_turns must be an integer of at least 0");
    for (auto const& body : { Json::Value{}, Json::Value{ Json::arrayValue }, test_support::field("api_key", "secret"), test_support::field("model", 123), test_support::field("request_timeout", 0), test_support::field("idle_reset_minutes", Json::Int64{ INT64_MAX }) }) {
        CHECK(test_support::request_admin("/api/admin/bot-config", drogon::Put, body)->statusCode() == drogon::k400BadRequest);
    }
    CHECK(settings->Current() == before);
}

DROGON_TEST(ReplySettingsReportStorageFailureAndConcurrentSaveAs409) {
    auto const db{ drogon::app().getPlugin<storage::Database>()->Client() };
    auto const settings{ drogon::app().getPlugin<controllers::AssistantBot>()->Settings() };
    auto const before{ settings->Current() };
    db->execSqlSync("PRAGMA query_only = ON");
    auto const failed{ test_support::request_admin("/api/admin/bot-config", drogon::Put, test_support::field("model", "cannot-save")) };
    db->execSqlSync("PRAGMA query_only = OFF");
    CHECK(failed->statusCode() == drogon::k500InternalServerError);
    CHECK(settings->Current() == before);
    std::promise<void> slow_done{};
    db->execSqlAsync(
        "WITH RECURSIVE n(x) AS (SELECT 1 UNION ALL SELECT x + 1 FROM n WHERE x < 5000000) SELECT count(*) FROM n",
        [&slow_done](drogon::orm::Result const&) { slow_done.set_value(); },
        [&slow_done](drogon::orm::DrogonDbException const&) { slow_done.set_value(); }
    );
    std::promise<void> saved{};
    drogon::async_run([settings, &saved]() -> drogon::Task<> {
        co_await settings->Update(test_support::field("model", "saving"));
        saved.set_value();
    });
    auto const conflict{ test_support::request_admin("/api/admin/bot-config", drogon::Put, Json::Value{ Json::objectValue }) };
    CHECK(conflict->statusCode() == drogon::k409Conflict);
    CHECK(settings->Current() == before);
    saved.get_future().wait();
    slow_done.get_future().wait();
    test_support::success(test_support::request_admin("/api/admin/bot-config", drogon::Put, Json::Value{ Json::objectValue }));
}

auto main(int argc, char** argv) -> int {
    test_support::load_config();
    return test_support::run_tests_with_app(argc, argv);
}
