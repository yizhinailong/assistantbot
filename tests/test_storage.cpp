/**
 * @file test_storage.cpp
 * @brief Check the database: the plugin, opening and migrating, and the settings rows.
 *
 * The app runs with `tests/config.json`, so `storage::Database` opens `test_support::DATA_DIR`.
 * The other tests open databases of their own in fresh temporary directories.
 */
#include <filesystem>
#include <format>
#include <optional>
#include <stdexcept>
#include <string>

#include <drogon/drogon.h>
#include <drogon/drogon_test.h>
#include <drogon/utils/coroutine.h>
#include <json/json.h>

#include "storage/database.hpp"
#include "storage/query.hpp"
#include "storage/schema.hpp"
#include "storage/settings.hpp"
#include "support.hpp"

namespace {

    /**
     * @brief Try a config on a plugin that is not registered with the app.
     * @param config Plugin config to pass to `initAndStart`.
     * @return The `std::invalid_argument` message, or `(accepted)` if nothing was thrown.
     */
    auto rejection(Json::Value const& config) -> std::string {
        try {
            storage::Database plugin{};
            plugin.initAndStart(config);
        } catch (std::invalid_argument const& error) {
            return error.what();
        }
        return "(accepted)";
    }

    /**
     * @brief Run a migration and report how it failed.
     * @param db Client of the database.
     * @return The `std::runtime_error` message, or `(migrated)` if nothing was thrown.
     */
    auto migrate_error(drogon::orm::DbClient& db) -> std::string {
        try {
            storage::migrate(db);
        } catch (std::runtime_error const& error) {
            return error.what();
        }
        return "(migrated)";
    }

    /**
     * @brief Check success and error continuations from an application I/O loop.
     * @param db Client retained by the test thread throughout execution.
     * @return Whether every database await resumed on the original loop.
     */
    auto keeps_callers_loop(drogon::orm::DbClientPtr db) -> drogon::Task<bool> {
        auto* const origin{ drogon::app().getIOLoop(0) };
        co_await drogon::switchThreadCoro(origin);
        auto const result{ co_await storage::query(db, "SELECT ? AS n", 42) };
        bool on_origin{ trantor::EventLoop::getEventLoopOfCurrentThread() == origin && result.at(0)["n"].as<int>() == 42 };
        Json::Value value{ Json::objectValue };
        value["history_turns"] = 4;
        co_await storage::save_setting(db, "loop-check", value);
        on_origin = on_origin && trantor::EventLoop::getEventLoopOfCurrentThread() == origin;
        bool rejected{};
        try {
            co_await storage::query(db, "SELECT * FROM nonexistent_loop_test_table");
        } catch (drogon::orm::DrogonDbException const&) {
            rejected = true;
        }
        co_return on_origin && rejected && trantor::EventLoop::getEventLoopOfCurrentThread() == origin;
    }

} // namespace

DROGON_TEST(PluginOpensDataDir) {
    auto* const plugin{ drogon::app().getPlugin<storage::Database>() };
    REQUIRE(plugin != nullptr);
    auto const client{ plugin->Client() };
    REQUIRE(client != nullptr);
    CHECK(std::filesystem::is_regular_file(std::filesystem::path{ test_support::DATA_DIR } / storage::DATABASE_FILE));
    CHECK(storage::schema_version(*client) == storage::latest_schema_version());
}

DROGON_TEST(PluginRejectsInvalidConfig) {
    CHECK(rejection(Json::Value{ Json::arrayValue }) == "storage::Database: config must be an object");

    Json::Value config{ Json::objectValue };
    config["data_dir"] = 1;
    CHECK(rejection(config) == "storage::Database: data_dir must be a non-empty string");
    config["data_dir"] = "";
    CHECK(rejection(config) == "storage::Database: data_dir must be a non-empty string");
}

DROGON_TEST(OpenCreatesDirectoryAndMigrates) {
    // The space checks that the file name is quoted in drogon's connection string.
    auto const dir{ test_support::fresh_directory("open") / "new dir" / "data" };
    auto const client{ storage::open_database(dir) };
    CHECK(std::filesystem::is_regular_file(dir / storage::DATABASE_FILE));
    CHECK(storage::schema_version(*client) == storage::latest_schema_version());
    CHECK(!storage::load_setting(*client, "missing").has_value());

    // A database at the latest version needs no migration.
    CHECK(migrate_error(*client) == "(migrated)");
    CHECK(storage::schema_version(*client) == storage::latest_schema_version());
}

DROGON_TEST(OpenRejectsUnopenableFile) {
    // A directory where the database file belongs makes SQLite fail to open it.
    auto const dir{ test_support::fresh_directory("unopenable") };
    std::filesystem::create_directory(dir / storage::DATABASE_FILE);
    std::string message{ "(opened)" };
    try {
        static_cast<void>(storage::open_database(dir));
    } catch (std::runtime_error const& error) {
        message = error.what();
    }
    auto const expected{ std::format("storage::Database: cannot open {}: unable to open database file", (dir / storage::DATABASE_FILE).string()) };
    CHECK(message == expected);
}

DROGON_TEST(RejectsNewerSchema) {
    auto const client{ storage::open_database(test_support::fresh_directory("newer")) };
    client->execSqlSync("PRAGMA user_version = 99");
    auto const expected{ std::format("storage::Database: the database has schema version 99, newer than the {} this build knows", storage::latest_schema_version()) };
    CHECK(migrate_error(*client) == expected);
}

DROGON_TEST(SettingsSurviveReopening) {
    auto const dir{ test_support::fresh_directory("reopen") };
    Json::Value value{ Json::objectValue };
    value["history_turns"] = 3;
    value["system_prompt"] = "你好";
    {
        auto const client{ storage::open_database(dir) };
        drogon::sync_wait(storage::save_setting(client, "bot", value));
    }

    auto const client{ storage::open_database(dir) };
    auto const loaded{ storage::load_setting(*client, "bot") };
    REQUIRE(loaded.has_value());
    CHECK(*loaded == value);
}

DROGON_TEST(SaveReplacesSetting) {
    auto const client{ storage::open_database(test_support::fresh_directory("replace")) };
    Json::Value first{ Json::objectValue };
    first["max_reply_chars"] = 100;
    Json::Value second{ Json::objectValue };
    second["request_timeout"] = 5;
    drogon::sync_wait(storage::save_setting(client, "bot", first));
    drogon::sync_wait(storage::save_setting(client, "bot", second));

    auto const loaded{ storage::load_setting(*client, "bot") };
    REQUIRE(loaded.has_value());
    CHECK(*loaded == second);
    CHECK(client->execSqlSync("SELECT count(*) AS n FROM settings").at(0)["n"].as<int>() == 1);
}

DROGON_TEST(LoadRejectsInvalidJson) {
    auto const client{ storage::open_database(test_support::fresh_directory("invalid")) };
    client->execSqlSync("INSERT INTO settings (name, value) VALUES ('bot', '{not json')");
    CHECK_THROWS_AS(storage::load_setting(*client, "bot"), std::runtime_error);
}

DROGON_TEST(StoragePreservesCallerLoopOnSuccessAndFailure) {
    auto const client{ storage::open_database(test_support::fresh_directory("caller-loop")) };
    CHECK(drogon::sync_wait(keeps_callers_loop(client)));
    auto const saved{ storage::load_setting(*client, "loop-check") };
    REQUIRE(saved.has_value());
    CHECK((*saved)["history_turns"] == 4);
}

auto main(int argc, char** argv) -> int {
    test_support::load_config();
    return test_support::run_tests_with_app(argc, argv);
}
