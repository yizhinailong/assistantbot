/**
 * @file test_access_control.cpp
 * @brief Check persistent access policies, failed writes, and concurrent updates.
 */
#include <future>
#include <memory>
#include <stdexcept>
#include <string>

#include <drogon/drogon_test.h>
#include <drogon/utils/coroutine.h>

#include "storage/database.hpp"
#include "storage/settings.hpp"
#include "support.hpp"
#include "workflow/access_control.hpp"

DROGON_TEST(AccessDefaultsAndBlacklistPriority) {
    auto const db{ storage::open_database(test_support::fresh_directory("access-priority")) };
    workflow::AccessControl access{ db };
    auto const initial{ access.Current() };
    CHECK(initial->mode == workflow::AccessMode::AllFriends);
    CHECK(initial->Allows(12345));

    drogon::sync_wait(access.SetListed(workflow::AccessList::Blacklist, 12345, true));
    CHECK(!access.Current()->Allows(12345));
    CHECK(access.Current()->Allows(54321));
    CHECK(initial->Allows(12345));

    drogon::sync_wait(access.SetMode(workflow::AccessMode::Allowlist));
    CHECK(!access.Current()->Allows(54321));
    drogon::sync_wait(access.SetListed(workflow::AccessList::Allowlist, 12345, true));
    CHECK(!access.Current()->Allows(12345));
    drogon::sync_wait(access.SetListed(workflow::AccessList::Blacklist, 12345, false));
    CHECK(access.Current()->Allows(12345));
    CHECK(!access.Current()->Allows(54321));
}

DROGON_TEST(AccessPersistsAcrossReopeningAndKeepsListsInBothModes) {
    auto const directory{ test_support::fresh_directory("access-reopen") };
    {
        auto const db{ storage::open_database(directory) };
        workflow::AccessControl access{ db };
        drogon::sync_wait(access.SetListed(workflow::AccessList::Allowlist, 9007199254740993LL, true));
        drogon::sync_wait(access.SetListed(workflow::AccessList::Allowlist, 9007199254740993LL, true));
        drogon::sync_wait(access.SetListed(workflow::AccessList::Blacklist, 54321, true));
        drogon::sync_wait(access.SetMode(workflow::AccessMode::Allowlist));
    }
    auto const reopened{ storage::open_database(directory) };
    workflow::AccessControl access{ reopened };
    CHECK(access.Current()->mode == workflow::AccessMode::Allowlist);
    CHECK(access.Current()->allowlist.size() == 1);
    CHECK(access.Current()->Allows(9007199254740993LL));
    CHECK(!access.Current()->Allows(54321));
    drogon::sync_wait(access.SetMode(workflow::AccessMode::AllFriends));
    CHECK(access.Current()->allowlist.contains(9007199254740993LL));
    CHECK(!access.Current()->Allows(54321));
    drogon::sync_wait(access.SetListed(workflow::AccessList::Allowlist, 54321, false));
    CHECK(access.Current()->blacklist.contains(54321));
}

DROGON_TEST(AccessRejectsInvalidSavedPolicyInsteadOfOpeningAccess) {
    auto const db{ storage::open_database(test_support::fresh_directory("access-invalid")) };
    Json::Value saved{ Json::objectValue };
    saved["mode"] = "allowlist";
    saved["allowlist"] = Json::Value{ Json::arrayValue };
    saved["blacklist"] = Json::Value{ Json::arrayValue };
    saved["blacklist"].append("0");
    drogon::sync_wait(storage::save_setting(db, workflow::ACCESS_SETTINGS_NAME, saved));
    CHECK_THROWS_AS(workflow::AccessControl{ db }, std::invalid_argument);
    saved["blacklist"][0] = Json::Int64{ 12345 };
    drogon::sync_wait(storage::save_setting(db, workflow::ACCESS_SETTINGS_NAME, saved));
    CHECK_THROWS_AS(workflow::AccessControl{ db }, std::invalid_argument);
    saved["blacklist"] = Json::Value{ Json::arrayValue };
    saved["mode"] = "everyone";
    drogon::sync_wait(storage::save_setting(db, workflow::ACCESS_SETTINGS_NAME, saved));
    CHECK_THROWS_AS(workflow::AccessControl{ db }, std::invalid_argument);
}

DROGON_TEST(AccessSaveFailurePreservesPolicyAndReleasesWriteClaim) {
    auto const db{ storage::open_database(test_support::fresh_directory("access-failure")) };
    workflow::AccessControl access{ db };
    auto const initial{ access.Current() };
    db->execSqlSync("PRAGMA query_only = ON");
    CHECK_THROWS_AS(drogon::sync_wait(access.SetMode(workflow::AccessMode::Allowlist)), drogon::orm::DrogonDbException);
    CHECK(access.Current() == initial);
    CHECK(!storage::load_setting(*db, workflow::ACCESS_SETTINGS_NAME).has_value());
    db->execSqlSync("PRAGMA query_only = OFF");
    drogon::sync_wait(access.SetMode(workflow::AccessMode::Allowlist));
    CHECK(!access.Current()->Allows(12345));
    auto const saved{ storage::load_setting(*db, workflow::ACCESS_SETTINGS_NAME) };
    REQUIRE(saved.has_value());
    CHECK((*saved)["mode"].asString() == "allowlist");
}

DROGON_TEST(AccessRejectsOverlappingChangesWithoutLosingUpdates) {
    auto const db{ storage::open_database(test_support::fresh_directory("access-concurrent")) };
    auto const access{ std::make_shared<workflow::AccessControl>(db) };
    auto const before{ access->Current() };
    std::promise<void> slow_done{};
    db->execSqlAsync(
        "WITH RECURSIVE n(x) AS (SELECT 1 UNION ALL SELECT x + 1 FROM n WHERE x < 1000000) SELECT count(*) FROM n",
        [&slow_done](drogon::orm::Result const&) { slow_done.set_value(); },
        [&slow_done](drogon::orm::DrogonDbException const&) { slow_done.set_value(); }
    );
    std::promise<void> saved{};
    drogon::async_run([access, &saved]() -> drogon::Task<> {
        co_await access->SetListed(workflow::AccessList::Blacklist, 12345, true);
        saved.set_value();
    });
    CHECK(access->Current() == before);
    CHECK_THROWS_AS(drogon::sync_wait(access->SetMode(workflow::AccessMode::Allowlist)), workflow::AccessConflict);
    saved.get_future().wait();
    slow_done.get_future().wait();
    drogon::sync_wait(access->SetMode(workflow::AccessMode::Allowlist));
    workflow::AccessControl const restarted{ db };
    CHECK(restarted.Current()->mode == workflow::AccessMode::Allowlist);
    CHECK(restarted.Current()->blacklist.contains(12345));
}

auto main(int argc, char** argv) -> int {
    return drogon::test::run(argc, argv);
}
