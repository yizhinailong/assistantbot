/**
 * @file test_bot_settings.cpp
 * @brief Check how `workflow::BotSettings` overlays the saved overrides on the plugin config.
 *
 * Runs without the app. Each test opens a database of its own in a fresh temporary directory.
 */
#include <future>
#include <memory>
#include <stdexcept>
#include <string>
#include <string_view>

#include <drogon/drogon_test.h>
#include <drogon/utils/coroutine.h>
#include <json/json.h>

#include "storage/database.hpp"
#include "storage/settings.hpp"
#include "support.hpp"
#include "workflow/bot_settings.hpp"

namespace {

    /**
     * @brief Query that keeps SQLite busy for a moment, so that statements sent after it wait.
     */
    constexpr char const* SLOW_QUERY{
        "WITH RECURSIVE n(x) AS (SELECT 1 UNION ALL SELECT x + 1 FROM n WHERE x < 1000000) SELECT count(*) FROM n"
    };

    /**
     * @brief Plugin config that sets two keys and leaves the rest to the defaults.
     * @return `{"history_turns": 4, "max_reply_chars": 300}`.
     */
    auto defaults() -> Json::Value {
        Json::Value config{ Json::objectValue };
        config["history_turns"] = 4;
        config["max_reply_chars"] = 300;
        return config;
    }

    /**
     * @brief Build an object with one key.
     * @param key Key to set.
     * @param value Its value.
     * @return The object.
     */
    auto one(char const* key, Json::Value const& value) -> Json::Value {
        Json::Value object{ Json::objectValue };
        object[key] = value;
        return object;
    }

    /**
     * @brief Create settings and report how they were rejected.
     * @param config Plugin config from config.json.
     * @param db Database with the overrides.
     * @return The `std::invalid_argument` message, or `(accepted)` if nothing was thrown.
     */
    auto construction_error(
        Json::Value const& config,
        drogon::orm::DbClientPtr const& db
    ) -> std::string {
        try {
            workflow::BotSettings const settings{ config, db };
        } catch (std::invalid_argument const& error) {
            return error.what();
        }
        return "(accepted)";
    }

    /**
     * @brief Update settings and report how the update was rejected.
     * @param settings Settings to update.
     * @param overrides New overrides.
     * @return The `std::invalid_argument` message, or `(accepted)` if nothing was thrown.
     */
    auto update_error(
        workflow::BotSettings& settings,
        Json::Value const& overrides
    ) -> std::string {
        try {
            drogon::sync_wait(settings.Update(overrides));
        } catch (std::invalid_argument const& error) {
            return error.what();
        }
        return "(accepted)";
    }

} // namespace

DROGON_TEST(DefaultsWithoutOverrides) {
    auto const db{ storage::open_database(test_support::fresh_directory("settings-defaults")) };
    workflow::BotSettings const settings{ defaults(), db };
    auto const config{ settings.Current() };
    CHECK(config->history_turns == 4);
    CHECK(config->max_reply_chars == 300);
    CHECK(config->system_prompt == workflow::DEFAULT_SYSTEM_PROMPT);
    CHECK(settings.Overrides() == Json::Value{ Json::objectValue });

    // Drogon passes a null config when config.json gives the plugin none.
    workflow::BotSettings const empty{ Json::Value{}, db };
    CHECK(empty.Current()->history_turns == 10);
}

DROGON_TEST(RejectsInvalidDefaults) {
    auto const db{ storage::open_database(test_support::fresh_directory("settings-invalid-defaults")) };
    CHECK(construction_error(one("history_turns", -1), db) == "controllers::AssistantBot: history_turns must be an integer of at least 0");
}

DROGON_TEST(SavedOverridesApply) {
    auto const db{ storage::open_database(test_support::fresh_directory("settings-saved")) };
    auto const saved{ one("max_reply_chars", 50) };
    drogon::sync_wait(storage::save_setting(db, workflow::BOT_SETTINGS_NAME, saved));

    workflow::BotSettings const settings{ defaults(), db };
    CHECK(settings.Current()->max_reply_chars == 50);
    CHECK(settings.Current()->history_turns == 4);
    CHECK(settings.Overrides() == saved);
}

DROGON_TEST(RejectsInvalidSavedOverrides) {
    auto const db{ storage::open_database(test_support::fresh_directory("settings-saved-invalid")) };
    drogon::sync_wait(storage::save_setting(db, workflow::BOT_SETTINGS_NAME, one("max_reply_chars", 0)));
    CHECK(construction_error(defaults(), db) == "controllers::AssistantBot: max_reply_chars must be an integer of at least 1 (in the settings saved in the database)");

    drogon::sync_wait(storage::save_setting(db, workflow::BOT_SETTINGS_NAME, one("data_dir", "x")));
    CHECK(construction_error(defaults(), db) == "controllers::AssistantBot: data_dir is not a setting (in the settings saved in the database)");
}

DROGON_TEST(UpdateSavesAndApplies) {
    auto const db{ storage::open_database(test_support::fresh_directory("settings-update")) };
    workflow::BotSettings settings{ defaults(), db };
    auto const before{ settings.Current() };

    auto const overrides{ one("system_prompt", "Be brief.") };
    drogon::sync_wait(settings.Update(overrides));
    CHECK(settings.Current()->system_prompt == "Be brief.");
    CHECK(settings.Current()->history_turns == 4);
    CHECK(settings.Overrides() == overrides);
    // A reply that took a snapshot before the update keeps it.
    CHECK(before->system_prompt == workflow::DEFAULT_SYSTEM_PROMPT);

    // The overrides are in the database, so a restart keeps them.
    workflow::BotSettings const restarted{ defaults(), db };
    CHECK(restarted.Current()->system_prompt == "Be brief.");

    // Empty overrides restore config.json.
    drogon::sync_wait(settings.Update(Json::Value{ Json::objectValue }));
    CHECK(settings.Current()->system_prompt == workflow::DEFAULT_SYSTEM_PROMPT);
    workflow::BotSettings const restored{ defaults(), db };
    CHECK(restored.Overrides() == Json::Value{ Json::objectValue });
}

DROGON_TEST(UpdateRejectsInvalidOverrides) {
    auto const db{ storage::open_database(test_support::fresh_directory("settings-update-invalid")) };
    workflow::BotSettings settings{ defaults(), db };
    auto const saved{ one("request_timeout", 5) };
    drogon::sync_wait(settings.Update(saved));

    CHECK(update_error(settings, Json::Value{ Json::arrayValue }) == "controllers::AssistantBot: settings must be an object");
    CHECK(update_error(settings, one("data_dir", "x")) == "controllers::AssistantBot: data_dir is not a setting");
    CHECK(update_error(settings, one("history_turns", "4")) == "controllers::AssistantBot: history_turns must be an integer of at least 0");

    // Neither memory nor the database changed.
    CHECK(settings.Current()->request_timeout == 5.0);
    CHECK(settings.Overrides() == saved);
    CHECK(storage::load_setting(*db, workflow::BOT_SETTINGS_NAME) == saved);
}

DROGON_TEST(UpdateRejectsSecondSaveInFlight) {
    auto const db{ storage::open_database(test_support::fresh_directory("settings-concurrent")) };
    auto const settings{ std::make_shared<workflow::BotSettings>(defaults(), db) };

    // The database has one connection, so the first update's write waits behind this query.
    std::promise<void> slow_done{};
    db->execSqlAsync(
        SLOW_QUERY,
        [&slow_done](drogon::orm::Result const&) { slow_done.set_value(); },
        [&slow_done](drogon::orm::DrogonDbException const&) { slow_done.set_value(); }
    );
    std::promise<void> first_done{};
    drogon::async_run([settings, &first_done]() -> drogon::Task<> {
        co_await settings->Update(one("max_reply_chars", 10));
        first_done.set_value();
    });

    CHECK_THROWS_AS(drogon::sync_wait(settings->Update(one("max_reply_chars", 20))), std::runtime_error);
    first_done.get_future().wait();
    slow_done.get_future().wait();
    CHECK(settings->Current()->max_reply_chars == 10);
    CHECK(storage::load_setting(*db, workflow::BOT_SETTINGS_NAME) == one("max_reply_chars", 10));
}

auto main(int argc, char** argv) -> int {
    return drogon::test::run(argc, argv);
}
