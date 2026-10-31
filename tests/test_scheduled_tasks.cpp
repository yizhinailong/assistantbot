/**
 * @file test_scheduled_tasks.cpp
 * @brief Verify atomic due delivery, daily advancement, restart recovery, and timer draining.
 */
#include <thread>

#include "common/time.hpp"
#include "inbox.hpp"
#include "storage/database.hpp"
#include "storage/query.hpp"
#include "support.hpp"
#include "workflow/scheduled_tasks.hpp"

namespace {
    drogon::orm::DbClientPtr database{};                   ///< Independent scheduler database retained on the main thread.
    std::shared_ptr<workflow::ScheduledTasks> scheduler{}; ///< Timer under test.
    test_support::Inbox<std::string> deliveries{};         ///< Actual handler invocations.
} // namespace

DROGON_TEST(SchedulerRetainsRecoveryAndAdvancesDailyWithoutReplaying) {
    auto records{ drogon::sync_wait(scheduler->List(66001)) };
    REQUIRE(records.size() == 1);
    CHECK(records[0]["status"] == "failed");
    CHECK(records[0]["error"].asString().contains("restart"));
    auto one{ drogon::sync_wait(scheduler->Create({ .user_id = 66002 }, common::unix_seconds() + 3600, "once", false)) };
    auto daily{ drogon::sync_wait(scheduler->Create({ .user_id = 66003 }, common::unix_seconds() + 3600, "daily", true)) };
    auto const missed{ common::unix_seconds() - 2 * 86400 };
    drogon::sync_wait(storage::query(database, "UPDATE scheduled_tasks SET due_at=? WHERE id IN (?,?)", missed, std::stoll(one["id"].asString()), std::stoll(daily["id"].asString())));
    auto wake{ drogon::sync_wait(scheduler->Create({ .user_id = 66004 }, common::unix_seconds() + 3600, "wake", false)) };
    auto first{ deliveries.Pop(std::chrono::seconds{ 5 }) };
    auto second{ deliveries.Pop(std::chrono::seconds{ 5 }) };
    REQUIRE(first);
    REQUIRE(second);
    CHECK(*first == "once");
    CHECK(*second == "daily");
    for (int i{}; i < 100 && scheduler->Busy(); ++i) {
        std::this_thread::sleep_for(std::chrono::milliseconds{ 10 });
    }
    CHECK(!scheduler->Busy());
    records = drogon::sync_wait(scheduler->List(66002));
    CHECK(records[0]["status"] == "done");
    records = drogon::sync_wait(scheduler->List(66003));
    CHECK(records[0]["status"] == "pending");
    CHECK(records[0]["due_at"].asInt64() > common::unix_seconds());
    CHECK((records[0]["due_at"].asInt64() - missed) % 86400 == 0);
    CHECK(!deliveries.Pop(std::chrono::milliseconds{ 100 }));
    CHECK(drogon::sync_wait(scheduler->Cancel(std::stoll(daily["id"].asString()))));
    CHECK(drogon::sync_wait(scheduler->Cancel(std::stoll(wake["id"].asString()))));
}

auto main(int argc, char** argv) -> int {
    test_support::load_config();
    auto const directory{ std::filesystem::temp_directory_path() / "assistantbot-test" / "durable-scheduler" };
    std::filesystem::remove_all(directory);
    database = storage::open_database(directory);
    database->execSqlSync("INSERT INTO scheduled_tasks(session_id,sender_id,due_at,content,daily,status,error,created_at) VALUES(66001,66001,1,'interrupted',0,'running','',1)");
    scheduler = std::make_shared<workflow::ScheduledTasks>(database, [](workflow::Peer, std::string content) -> drogon::Task<> {deliveries.Push(std::move(content));co_return; });
    scheduler->Start();
    auto result{ test_support::run_tests_with_app(argc, argv, [] {scheduler->Stop();for(int i{};i<200&&scheduler->Busy();++i){std::this_thread::sleep_for(std::chrono::milliseconds{10});} }) };
    scheduler.reset();
    database.reset();
    return result;
}
