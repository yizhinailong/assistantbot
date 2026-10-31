/**
 * @file test_admin_logs.cpp
 * @brief Verify retained log bounds, exact QQ filtering, redaction, and runtime levels.
 */
#include <atomic>
#include <mutex>
#include <string>
#include <vector>

#include <drogon/drogon_test.h>

#include "admin/live_events.hpp"
#include "admin_support.hpp"

namespace {

    std::atomic_bool g_original_saw_secret{}; ///< Whether the configured sink received unmodified bytes.

} // namespace

DROGON_TEST(LogFiltersRespectSeverityKeywordAndCompleteQqNumbers) {
    LOG_INFO << "filter-check (12345) INFO line with TRACE in the body";
    LOG_WARN << "filter-check (9123456) excluded longer QQ";
    LOG_WARN << "filter-check (12345) accepted warning";
    LOG_ERROR << "filter-check (12345) accepted error";
    LOG_ERROR << "unrelated (12345)";
    auto const response{ test_support::success(test_support::request_admin("/api/admin/logs?level=WARN&keyword=filter-check&qq=0012345")) };
    REQUIRE(response["entries"].size() == 2);
    CHECK(response["entries"][0]["level"] == "WARN");
    CHECK(response["entries"][1]["level"] == "ERROR");
    CHECK(response["filters"]["qq"] == "12345");
    auto const info{ test_support::success(test_support::request_admin("/api/admin/logs?keyword=TRACE")) };
    REQUIRE(!info["entries"].empty());
    CHECK(info["entries"][info["entries"].size() - 1]["level"] == "INFO");
    auto const cursor{ response["cursor"].asString() };
    LOG_WARN << "filter-check (12345) next";
    auto const next{ test_support::success(test_support::request_admin("/api/admin/logs?keyword=filter-check&qq=12345&after=" + cursor)) };
    REQUIRE(next["entries"].size() == 1);
    CHECK(next["entries"][0]["message"].asString().find("next") != std::string::npos);
}

DROGON_TEST(LogCopyRedactsConfiguredSecretsAndPreservesTheOriginalSink) {
    LOG_WARN << "redaction-check " << test_support::ADMIN_TOKEN << " " << test_support::SECRET;
    auto const result{ test_support::success(test_support::request_admin("/api/admin/logs?keyword=redaction-check")) };
    REQUIRE(result["entries"].size() == 1);
    auto const message{ result["entries"][0]["message"].asString() };
    CHECK(message.find(test_support::ADMIN_TOKEN) == std::string::npos);
    CHECK(message.find(test_support::SECRET) == std::string::npos);
    CHECK(message.find("[redacted]") != std::string::npos);
    CHECK(g_original_saw_secret.load());
}

DROGON_TEST(LogBufferEvictsOldRecordsAndReportsGaps) {
    auto const before{ admin::read_logs({}, std::nullopt, 1)["cursor"].asString() };
    for (int i{}; i < 5010; ++i) {
        LOG_WARN << "ring-check " << i;
    }
    auto const records{ test_support::success(test_support::request_admin("/api/admin/logs?limit=5000")) };
    CHECK(records["entries"].size() == 5000);
    auto const catch_up{ test_support::success(test_support::request_admin("/api/admin/logs?after=" + before + "&limit=200")) };
    CHECK(catch_up["truncated"].asBool());
    CHECK(catch_up["entries"].size() == 200);
    CHECK(std::stoll(catch_up["cursor"].asString()) > std::stoll(before));
}

DROGON_TEST(LogRoutesValidateFiltersAndChangeTheRunningLevel) {
    CHECK(test_support::request_admin("/api/admin/logs", drogon::Get, {}, "")->statusCode() == drogon::k401Unauthorized);
    CHECK(test_support::request_admin("/api/admin/log-level", drogon::Put, test_support::field("level", "WARN"), "")->statusCode() == drogon::k401Unauthorized);
    for (auto const* query : { "level=verbose", "qq=-1", "qq=0", "limit=0", "limit=5001", "after=", "after=-1" }) {
        CHECK(test_support::request_admin(std::string{ "/api/admin/logs?" } + query)->statusCode() == drogon::k400BadRequest);
    }
    CHECK(test_support::request_admin("/api/admin/logs?keyword=" + std::string(257, 'x'))->statusCode() == drogon::k400BadRequest);
    for (auto const& body : { Json::Value{}, Json::Value{ Json::arrayValue }, test_support::field("level", "warn"), test_support::field("level", 2) }) {
        CHECK(test_support::request_admin("/api/admin/log-level", drogon::Put, body)->statusCode() == drogon::k400BadRequest);
    }
    auto const before{ admin::read_logs({}, std::nullopt, 1)["cursor"].asString() };
    CHECK(test_support::request_admin("/api/admin/log-level", drogon::Put, test_support::field("level", "ERROR"))->statusCode() == drogon::k200OK);
    LOG_INFO << "level-check should be suppressed";
    LOG_ERROR << "level-check should be captured";
    auto const logs{ test_support::success(test_support::request_admin("/api/admin/logs?keyword=level-check&after=" + before)) };
    REQUIRE(logs["entries"].size() == 1);
    CHECK(logs["entries"][0]["level"] == "ERROR");
    CHECK(logs["log_level"] == "ERROR");
    test_support::success(test_support::request_admin("/api/admin/log-level", drogon::Put, test_support::field("level", "INFO")));
}

auto main(int argc, char** argv) -> int {
    test_support::load_config();
    // Installed before Auth's beginning advice; the administrative copy must chain it.
    drogon::app().registerBeginningAdvice([] {
        trantor::Logger::setOutputFunction([](char const* data, std::uint64_t size) {
            std::string_view const text{ data, static_cast<std::size_t>(size) };
            if (text.find("redaction-check") != std::string_view::npos && text.find(test_support::ADMIN_TOKEN) != std::string_view::npos) {
                g_original_saw_secret = true;
            } }, [] {});
    });
    return test_support::run_tests_with_app(argc, argv);
}
