/**
 * @file test_usage.cpp
 * @brief Verify persistent usage, missing reports, and UTC interval boundaries.
 */
#include <filesystem>

#include <drogon/drogon_test.h>

#include "storage/database.hpp"
#include "storage/schema.hpp"
#include "storage/usage.hpp"
#include "support.hpp"

DROGON_TEST(UsageSurvivesMigrationAndReopening) {
    auto const directory{ test_support::fresh_directory("usage") };
    {
        // Start at the previously published schema, with a saved setting to preserve.
        auto const client{ drogon::orm::DbClient::newSqlite3Client(
            "filename='" + (directory / storage::DATABASE_FILE).string() + "'",
            1
        ) };
        client->execSqlSync("CREATE TABLE settings (name TEXT PRIMARY KEY NOT NULL, value TEXT NOT NULL) STRICT");
        client->execSqlSync("INSERT INTO settings VALUES ('bot', '{}')");
        client->execSqlSync("PRAGMA user_version = 1");
        storage::migrate(*client);
        CHECK(client->execSqlSync("SELECT count(*) AS n FROM settings").at(0)["n"].as<int>() == 1);

        // Include the start of the day, exclude the start of the next day.
        for (auto const timestamp : { 86399, 86400, 172799, 172800 }) {
            drogon::sync_wait(storage::record_usage(client, {
                                                                .timestamp = timestamp,
                                                                .user_id = 42,
                                                                .model = "test-model",
                                                                .succeeded = true,
                                                                .prompt_tokens = 5,
                                                                .completion_tokens = 3,
                                                                .total_tokens = 8,
                                                            }));
        }
        drogon::sync_wait(storage::record_usage(client, {
                                                            .timestamp = 90000,
                                                            .user_id = 43,
                                                            .model = "test-model",
                                                            .succeeded = false,
                                                        }));
        drogon::sync_wait(storage::record_usage(client, {
                                                            .timestamp = 90001,
                                                            .user_id = 43,
                                                            .model = "test-model",
                                                            .succeeded = true,
                                                        }));
    }
    auto const reopened{ storage::open_database(directory) };
    auto const usage{ drogon::sync_wait(storage::summarize_usage(reopened, 86400, 172800)) };
    CHECK(usage["requests"].asInt64() == 4);
    CHECK(usage["successful_requests"].asInt64() == 3);
    CHECK(usage["reported_requests"].asInt64() == 2);
    CHECK(usage["prompt_tokens"].asInt64() == 10);
    CHECK(usage["completion_tokens"].asInt64() == 6);
    CHECK(usage["total_tokens"].asInt64() == 16);
    CHECK(usage["timezone"].asString() == "UTC");
    auto const empty{ drogon::sync_wait(storage::summarize_usage(reopened, 0, 1)) };
    CHECK(empty["requests"].asInt64() == 0);
    CHECK(empty["total_tokens"].asInt64() == 0);
}

DROGON_TEST(UsageMigrationKeepsExistingReportsWithUnknownCacheHits) {
    auto const directory{ test_support::fresh_directory("usage-schema-two") };
    auto const db{ drogon::orm::DbClient::newSqlite3Client("filename='" + (directory / storage::DATABASE_FILE).string() + "'", 1) };
    db->execSqlSync("CREATE TABLE settings (name TEXT PRIMARY KEY NOT NULL, value TEXT NOT NULL) STRICT");
    db->execSqlSync("CREATE TABLE model_usage (id INTEGER PRIMARY KEY, timestamp INTEGER NOT NULL, user_id INTEGER NOT NULL, model TEXT NOT NULL, succeeded INTEGER NOT NULL CHECK (succeeded IN (0, 1)), prompt_tokens INTEGER, completion_tokens INTEGER, total_tokens INTEGER) STRICT");
    db->execSqlSync("CREATE INDEX model_usage_timestamp ON model_usage (timestamp)");
    db->execSqlSync("INSERT INTO model_usage VALUES (1, 86400, 42, 'old-model', 1, 5, 3, 8)");
    db->execSqlSync("PRAGMA user_version = 2");
    storage::migrate(*db);
    auto const old{ db->execSqlSync("SELECT * FROM model_usage WHERE id = 1") };
    REQUIRE(old.size() == 1);
    CHECK(old[0]["model"].as<std::string>() == "old-model");
    CHECK(old[0]["cached_tokens"].isNull());
    auto const totals{ drogon::sync_wait(storage::summarize_usage(db, 86400, 172800)) };
    CHECK(totals["total_tokens"].asInt64() == 8);
    CHECK(totals["cached_requests"].asInt64() == 0);
    CHECK(totals["cached_tokens"].asInt64() == 0);
}

auto main(int argc, char** argv) -> int {
    return drogon::test::run(argc, argv);
}
