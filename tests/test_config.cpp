/**
 * @file test_config.cpp
 * @brief Check how `parse_bot_config` reads and validates the plugin config.
 *
 * Runs without the app. Also parses the bot entries of the tracked configuration files.
 */
#include <chrono>
#include <stdexcept>
#include <string>

#include <drogon/drogon_test.h>
#include <json/json.h>

#include "support.hpp"
#include "workflow/bot_config.hpp"

namespace {

    constexpr char const* APP_CONFIG_PATH{ "config.json" }; ///< The app's configuration, relative to the repo root.

    /**
     * @brief Parse a config and report how it was rejected.
     * @param config Plugin config to parse.
     * @return The `std::invalid_argument` message, or `(accepted)` if nothing was thrown.
     */
    auto rejection(Json::Value const& config) -> std::string {
        try {
            static_cast<void>(workflow::parse_bot_config(config));
        } catch (std::invalid_argument const& error) {
            return error.what();
        }
        return "(accepted)";
    }

    /**
     * @brief Parse a config that sets only one key.
     * @param key Key to set.
     * @param value Value of the key.
     * @return Result of `rejection()`.
     */
    auto rejection_with(char const* key, Json::Value const& value) -> std::string {
        Json::Value config{ Json::objectValue };
        config[key] = value;
        return rejection(config);
    }

    /**
     * @brief Find the `controllers::AssistantBot` entry of a configuration file.
     * @param path File to read.
     * @return The entry, with its `name`, `dependencies`, and `config`; null when absent.
     */
    auto bot_entry(char const* path) -> Json::Value {
        for (auto const& plugin : test_support::read_json(path)["plugins"]) {
            if (plugin["name"].asString() == "controllers::AssistantBot") {
                return plugin;
            }
        }
        return Json::Value{};
    }

} // namespace

DROGON_TEST(DefaultsApply) {
    // Drogon passes a null config when config.json gives the plugin none.
    CHECK(rejection(Json::Value{}) == "(accepted)");

    auto const config{ workflow::parse_bot_config(Json::Value{ Json::objectValue }) };
    CHECK(config.system_prompt == workflow::DEFAULT_SYSTEM_PROMPT);
    CHECK(config.history_turns == 10);
    CHECK(config.idle_reset == std::chrono::minutes{ 30 });
    CHECK(config.request_timeout == 90.0);
    CHECK(!config.max_tokens.has_value());
    CHECK(config.max_reply_chars == 1500);
    CHECK(config.model.empty());
}

DROGON_TEST(ReadsEveryKey) {
    Json::Value json{ Json::objectValue };
    json["system_prompt"] = "";
    json["history_turns"] = 0;
    json["idle_reset_minutes"] = 5;
    json["request_timeout"] = 12.5;
    json["max_tokens"] = 256;
    json["max_reply_chars"] = 200;
    json["model"] = "chat-model";

    auto const config{ workflow::parse_bot_config(json) };
    CHECK(config.system_prompt.empty());
    CHECK(config.history_turns == 0);
    CHECK(config.idle_reset == std::chrono::minutes{ 5 });
    CHECK(config.request_timeout == 12.5);
    CHECK(config.max_tokens == 256);
    CHECK(config.max_reply_chars == 200);
    CHECK(config.model == "chat-model");
    json["max_tokens"] = Json::Value{};
    CHECK(!workflow::parse_bot_config(json).max_tokens.has_value());
}

DROGON_TEST(RejectsInvalidValues) {
    CHECK(rejection(Json::Value{ Json::arrayValue }) == "controllers::AssistantBot: config must be an object");
    CHECK(rejection_with("system_prompt", 1) == "controllers::AssistantBot: system_prompt must be a string");
    CHECK(rejection_with("request_timeout", 0) == "controllers::AssistantBot: request_timeout must be a positive number of seconds");
    CHECK(rejection_with("request_timeout", "90") == "controllers::AssistantBot: request_timeout must be a positive number of seconds");
    CHECK(rejection_with("history_turns", -1) == "controllers::AssistantBot: history_turns must be an integer of at least 0");
    CHECK(rejection_with("history_turns", "10") == "controllers::AssistantBot: history_turns must be an integer of at least 0");
    CHECK(rejection_with("idle_reset_minutes", 0) == "controllers::AssistantBot: idle_reset_minutes must be an integer of at least 1");
    CHECK(rejection_with("max_tokens", 0) == "controllers::AssistantBot: max_tokens must be an integer of at least 1");
    CHECK(rejection_with("max_reply_chars", 0) == "controllers::AssistantBot: max_reply_chars must be an integer of at least 1");
    CHECK(rejection_with("model", 123) == "controllers::AssistantBot: model must be a string");
}

DROGON_TEST(TrackedConfigsParse) {
    for (auto const* path : { test_support::CONFIG_PATH, APP_CONFIG_PATH }) {
        auto const entry{ bot_entry(path) };
        REQUIRE(entry.isObject());
        CHECK(rejection(entry["config"]) == "(accepted)");

        // Drogon initializes the dependencies first, so the plugin can use them.
        Json::Value dependencies{ Json::arrayValue };
        dependencies.append("drogon_onebot::OneBotClient");
        dependencies.append("drogon_openai::OpenAIClient");
        dependencies.append("storage::Database");
        CHECK(entry["dependencies"] == dependencies);
    }
}

auto main(int argc, char** argv) -> int {
    return drogon::test::run(argc, argv);
}
