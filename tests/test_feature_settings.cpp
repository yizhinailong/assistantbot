/**
 * @file test_feature_settings.cpp
 * @brief Verify feature snapshots, restart persistence, and document compare-and-swap.
 */
#include <drogon/drogon_test.h>

#include "storage/database.hpp"
#include "storage/feature_records.hpp"
#include "support.hpp"
#include "workflow/feature_settings.hpp"

DROGON_TEST(FeatureSettingsKeepSnapshotsAndRejectStaleSaves) {
    auto const db{ storage::open_database(test_support::fresh_directory("feature-settings")) };
    workflow::FeatureSettings settings{ db };
    auto const initial{ settings.Current() };
    CHECK((*initial)["groups"].empty());
    CHECK(!(*initial)["tools"]["enabled"].asBool());
    Json::Value groups{ Json::objectValue };
    groups["123"]["enabled"] = true;
    groups["123"]["additional_prompt"] = "Be friendly.";
    drogon::sync_wait(settings.Update("groups", groups, 0));
    CHECK((*settings.Current())["groups"]["123"]["enabled"].asBool());
    CHECK((*initial)["groups"].empty());
    CHECK(settings.Describe("groups")["revision"].asString() == "1");
    CHECK_THROWS_AS(drogon::sync_wait(settings.Update("groups", groups, 0)), std::runtime_error);
    workflow::FeatureSettings restarted{ db };
    CHECK(restarted.Describe("groups") == settings.Describe("groups"));

    groups["123"]["enabled"] = "yes";
    CHECK_THROWS_AS(drogon::sync_wait(settings.Update("groups", groups)), std::invalid_argument);
    Json::Value models{ Json::objectValue };
    models["vision"]["api_key"] = "must-not-be-saved";
    CHECK_THROWS_AS(drogon::sync_wait(settings.Update("models", models)), std::invalid_argument);
    models["vision"].removeMember("api_key");
    models["vision"]["base_url"] = "https://secret@example.test/v1";
    CHECK_THROWS_AS(drogon::sync_wait(settings.Update("models", models)), std::invalid_argument);
    CHECK_THROWS_AS(settings.Describe("_revision"), std::invalid_argument);
    CHECK(settings.Describe("groups")["revision"].asString() == "1");
}

DROGON_TEST(FeatureDocumentsUseAtomicRevisionsAndCannotRecreateDeletedVersions) {
    auto const db{ storage::open_database(test_support::fresh_directory("feature-documents")) };
    Json::Value value{};
    value["aliases"] = Json::Value{ Json::arrayValue };
    value["aliases"].append("Alice");
    auto first{ drogon::sync_wait(storage::save_feature_record(db, "aliases", "123", value, 0)) };
    CHECK(first["revision"].asString() == "1");
    CHECK_THROWS_AS(drogon::sync_wait(storage::save_feature_record(db, "aliases", "123", value, 0)), std::runtime_error);
    value["aliases"].append("A");
    auto second{ drogon::sync_wait(storage::save_feature_record(db, "aliases", "123", value, 1)) };
    CHECK(second["revision"].asString() == "2");
    CHECK(!drogon::sync_wait(storage::delete_feature_record(db, "aliases", "123", 1)));
    CHECK(drogon::sync_wait(storage::delete_feature_record(db, "aliases", "123", 2)));
    CHECK_THROWS_AS(drogon::sync_wait(storage::save_feature_record(db, "aliases", "123", value, 2)), std::runtime_error);
    CHECK(drogon::sync_wait(storage::feature_records(db, "aliases")).empty());
    CHECK(drogon::sync_wait(storage::save_feature_record(db, "aliases", "123", value, 0))["revision"].asString() == "1");
}

auto main(int argc, char** argv) -> int {
    return drogon::test::run(argc, argv);
}
