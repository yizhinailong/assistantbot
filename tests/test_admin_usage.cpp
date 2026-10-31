/**
 * @file test_admin_usage.cpp
 * @brief Verify bounded UTC usage queries, cache totals, and independent today statistics.
 */
#include <drogon/drogon_test.h>

#include "admin_support.hpp"
#include "common/time.hpp"
#include "storage/database.hpp"
#include "storage/usage.hpp"

DROGON_TEST(UsageQueriesRequireAuthenticationAndValidBoundedIntervals) {
    CHECK(test_support::request_admin("/api/admin/usage", drogon::Get, {}, "")->statusCode() == drogon::k401Unauthorized);
    for (auto const* query : { "from=", "from=-1", "to=0", "from=5&to=5", "from=6&to=5", "from=0&to=31622401", "from=9223372036854775808" }) {
        CHECK(test_support::request_admin(std::string{ "/api/admin/usage?" } + query)->statusCode() == drogon::k400BadRequest);
    }
    auto const defaults{ test_support::success(test_support::request_admin("/api/admin/usage")) };
    CHECK(defaults["timezone"] == "UTC");
    CHECK(defaults["to"].asInt64() - defaults["from"].asInt64() == 30 * 86400);
}

DROGON_TEST(UsageGroupsUseTheSameUtcBoundariesAndPreserveUnknownCacheReports) {
    auto const db{ drogon::app().getPlugin<storage::Database>()->Client() };
    for (auto const timestamp : { 86399, 86400, 172799, 172800 }) {
        drogon::sync_wait(storage::record_usage(db, {
                                                        .timestamp = timestamp,
                                                        .user_id = 9007199254740993LL,
                                                        .model = "cached-model",
                                                        .succeeded = true,
                                                        .prompt_tokens = 5,
                                                        .completion_tokens = 3,
                                                        .total_tokens = 8,
                                                        .cached_tokens = timestamp == 86400 ? 3 : 0,
                                                    }));
    }
    drogon::sync_wait(storage::record_usage(db, { .timestamp = 90000, .user_id = 2, .model = "failed-model", .succeeded = false }));
    auto const today{ common::unix_seconds() / 86400 * 86400 };
    drogon::sync_wait(storage::record_usage(db, {
                                                    .timestamp = today,
                                                    .user_id = 3,
                                                    .model = "today-model",
                                                    .succeeded = true,
                                                    .prompt_tokens = 7,
                                                    .completion_tokens = 2,
                                                    .total_tokens = 9,
                                                }));
    auto const report{ test_support::success(test_support::request_admin("/api/admin/usage?from=86400&to=172800")) };
    CHECK(report["total"]["requests"].asInt64() == 3);
    CHECK(report["total"]["successful_requests"].asInt64() == 2);
    CHECK(report["total"]["reported_requests"].asInt64() == 2);
    CHECK(report["total"]["total_tokens"].asInt64() == 16);
    CHECK(report["total"]["cached_tokens"].asInt64() == 3);
    CHECK(report["total"]["cached_requests"].asInt64() == 2);
    REQUIRE(report["days"].size() == 1);
    CHECK(report["days"][0]["timestamp"].asInt64() == 86400);
    CHECK(report["days"][0]["total_tokens"] == report["total"]["total_tokens"]);
    REQUIRE(report["users"].size() == 2);
    CHECK(report["users"][0]["user_id"] == "2");
    CHECK(report["users"][0]["cached_requests"].asInt64() == 0);
    CHECK(report["users"][1]["user_id"] == "9007199254740993");
    CHECK(report["users"][1]["total_tokens"].asInt64() == 16);
    CHECK(report["today"]["requests"].asInt64() == 1);
    CHECK(report["today"]["from"].asInt64() == today);
    CHECK(report["today"]["cached_requests"].asInt64() == 0);
    auto const empty{ test_support::success(test_support::request_admin("/api/admin/usage?from=0&to=1")) };
    CHECK(empty["total"]["requests"].asInt64() == 0);
    CHECK(empty["days"].empty());
    CHECK(empty["users"].empty());
}

auto main(int argc, char** argv) -> int {
    test_support::load_config();
    return test_support::run_tests_with_app(argc, argv);
}
