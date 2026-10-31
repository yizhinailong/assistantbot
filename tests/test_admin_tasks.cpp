/**
 * @file test_admin_tasks.cpp
 * @brief Verify task auth, UTC validation, conversation scoping, cancellation, and access rechecks.
 */
#include <thread>

#include "admin_support.hpp"
#include "common/time.hpp"
#include "controllers/onebot/assistant_bot.hpp"
#include "storage/database.hpp"
#include "storage/query.hpp"
#include "workflow/scheduled_tasks.hpp"

DROGON_TEST(TaskApiValidatesAndCancelsDurableReminders) {
    CHECK(test_support::request_admin("/api/admin/tasks", drogon::Get, {}, "")->statusCode() == drogon::k401Unauthorized);
    Json::Value body{};
    body["session"] = "-44001";
    body["due_at"] = Json::Int64{ common::unix_seconds() + 3600 };
    body["content"] = "remember to drink water";
    auto response{ test_support::request_admin("/api/admin/tasks", drogon::Post, body) };
    REQUIRE(response->statusCode() == drogon::k200OK);
    auto const task{ *response->getJsonObject() };
    CHECK(task["session_id"] == "-44001");
    auto const id{ task["id"].asString() };
    auto const tasks{ test_support::request_admin("/api/admin/tasks?session=-44001")->getJsonObject() };
    REQUIRE(tasks);
    CHECK((*tasks)["tasks"].size() == 1);
    auto service{ drogon::app().getPlugin<controllers::AssistantBot>()->Tasks() };
    CHECK(!drogon::sync_wait(service->Cancel(std::stoll(id), 44001)));
    CHECK(test_support::request_admin("/api/admin/tasks/" + id + "/cancel", drogon::Post)->statusCode() == drogon::k200OK);
    CHECK(test_support::request_admin("/api/admin/tasks/" + id + "/cancel", drogon::Post)->statusCode() == drogon::k409Conflict);
    body["due_at"] = Json::Int64{ common::unix_seconds() - 1 };
    CHECK(test_support::request_admin("/api/admin/tasks", drogon::Post, body)->statusCode() == drogon::k400BadRequest);
    CHECK(test_support::request_admin("/api/admin/tasks?session=0")->statusCode() == drogon::k400BadRequest);
}

DROGON_TEST(DueTasksRecheckPauseAndRecordFailureOnce) {
    auto* bot{ drogon::app().getPlugin<controllers::AssistantBot>() };
    bot->SetPaused(true);
    auto task{ drogon::sync_wait(bot->Tasks()->Create({ .user_id = 44002 }, common::unix_seconds() + 3600, "must not send", false)) };
    auto const db{ drogon::app().getPlugin<storage::Database>()->Client() };
    drogon::sync_wait(storage::query(db, "UPDATE scheduled_tasks SET due_at = ? WHERE id = ?", common::unix_seconds() - 1, std::stoll(task["id"].asString())));
    // Create wakes polling after the test moves the prior deadline into the past.
    auto wake{ drogon::sync_wait(bot->Tasks()->Create({ .user_id = 44003 }, common::unix_seconds() + 3600, "wake", false)) };
    std::string status{};
    for (int i{}; i < 60; ++i) {
        auto rows{ drogon::sync_wait(bot->Tasks()->List(44002)) };
        status = rows[0]["status"].asString();
        if (status == "failed") {
            CHECK(rows[0]["error"].asString().contains("paused"));
            break;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds{ 50 });
    }
    bot->SetPaused(false);
    CHECK(status == "failed");
    CHECK(!drogon::sync_wait(bot->Tasks()->Cancel(std::stoll(task["id"].asString()))));
    CHECK(drogon::sync_wait(bot->Tasks()->Cancel(std::stoll(wake["id"].asString()))));
}

auto main(int argc, char** argv) -> int {
    test_support::load_config();
    return test_support::run_tests_with_app(argc, argv);
}
