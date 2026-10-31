/**
 * @file bot_settings.cpp
 * @brief Implement the current settings of the bot: config.json overlaid with the saved overrides.
 */
#include "workflow/bot_settings.hpp"

#include <algorithm>
#include <format>
#include <stdexcept>
#include <string>
#include <utility>

#include "storage/settings.hpp"

namespace workflow {

    namespace {

        /**
         * @brief Check that overrides are an object of keys that `parse_bot_config()` reads.
         * @param overrides Overrides to check.
         * @throws std::invalid_argument If they are not.
         */
        auto check_keys(Json::Value const& overrides) -> void {
            if (!overrides.isObject()) {
                throw std::invalid_argument{ "controllers::AssistantBot: settings must be an object" };
            }
            for (auto const& key : overrides.getMemberNames()) {
                if (std::ranges::find(BOT_CONFIG_KEYS, key) == BOT_CONFIG_KEYS.end()) {
                    throw std::invalid_argument{ std::format("controllers::AssistantBot: {} is not a setting", key) };
                }
            }
        }

        /**
         * @brief Validate overrides and build the settings they give.
         * @param defaults Plugin config from config.json, as an object.
         * @param overrides Overrides to apply on top of it.
         * @return The settings, as a snapshot.
         * @throws std::invalid_argument If the overrides have a wrong key or value.
         */
        auto build(
            Json::Value const& defaults,
            Json::Value const& overrides
        ) -> std::shared_ptr<BotConfig const> {
            check_keys(overrides);
            auto merged{ defaults };
            for (auto const& key : overrides.getMemberNames()) {
                merged[key] = overrides[key];
            }
            return std::make_shared<BotConfig const>(parse_bot_config(merged));
        }

    } // namespace

    /**
     * @brief Read the saved overrides and build the first snapshot.
     *
     * Blocks on the database: call it only on a thread that runs no event loop, such as
     * the one that runs `initAndStart`.
     * @param defaults Plugin config from config.json; null when it gives none.
     * @param db Database with the settings table.
     * @throws std::invalid_argument If `defaults` is invalid, with `parse_bot_config()`'s
     * message, or if the saved overrides are, with that message and a note that they come
     * from the database.
     * @throws drogon::orm::DrogonDbException If the overrides cannot be read.
     * @throws std::runtime_error If the saved overrides are not valid JSON.
     */
    BotSettings::BotSettings(Json::Value defaults, drogon::orm::DbClientPtr db)
        : m_defaults{ std::move(defaults) },
          m_db{ std::move(db) },
          m_overrides{ Json::objectValue } {
        static_cast<void>(parse_bot_config(m_defaults));
        if (m_defaults.isNull()) {
            m_defaults = Json::Value{ Json::objectValue };
        }
        if (auto saved{ storage::load_setting(*m_db, BOT_SETTINGS_NAME) }) {
            m_overrides = std::move(*saved);
        }
        try {
            m_current = build(m_defaults, m_overrides);
        } catch (std::invalid_argument const& error) {
            throw std::invalid_argument{ std::format("{} (in the settings saved in the database)", error.what()) };
        }
    }

    /**
     * @brief Get the settings in effect.
     * @return Snapshot that later updates leave unchanged.
     */
    auto BotSettings::Current() const -> std::shared_ptr<BotConfig const> {
        std::lock_guard lock{ m_mutex };
        return m_current;
    }

    /**
     * @brief Get the saved overrides.
     * @return Object of the keys that differ from config.json; empty when none.
     */
    auto BotSettings::Overrides() const -> Json::Value {
        std::lock_guard lock{ m_mutex };
        return m_overrides;
    }

    /**
     * @brief Describe the current values, saved overrides, and config.json defaults together.
     * @param fallback_model Model configured by the OpenAI plugin.
     * @return Administrative settings view without secrets.
     */
    auto BotSettings::Describe(std::string const& fallback_model) const -> Json::Value {
        auto encode{ [&fallback_model](BotConfig const& config) {
            Json::Value value{ Json::objectValue };
            value["system_prompt"] = config.system_prompt;
            value["history_turns"] = Json::UInt64{ config.history_turns };
            value["idle_reset_minutes"] = Json::Int64{ config.idle_reset.count() };
            value["request_timeout"] = config.request_timeout;
            value["max_tokens"] = config.max_tokens ? Json::Value{ Json::Int64{ *config.max_tokens } } : Json::Value{};
            value["max_reply_chars"] = Json::UInt64{ config.max_reply_chars };
            value["model"] = config.model.empty() ? fallback_model : config.model;
            return value;
        } };
        std::lock_guard lock{ m_mutex };
        Json::Value body{ Json::objectValue };
        body["values"] = encode(*m_current);
        body["overrides"] = m_overrides;
        body["defaults"] = encode(parse_bot_config(m_defaults));
        body["default_system_prompt"] = DEFAULT_SYSTEM_PROMPT;
        return body;
    }

    /**
     * @brief Validate, save, and apply new overrides, which replace the saved ones.
     *
     * Keys left out fall back to config.json, so `{}` restores it. Nothing changes when
     * the overrides are invalid, another update is saving, or saving fails. Replies under
     * way keep the snapshot they took.
     * @param overrides Object of keys from `BOT_CONFIG_KEYS`.
     * @return Task that ends when the overrides are saved and in effect.
     * @throws std::invalid_argument If `overrides` is not an object, has a key outside
     * `BOT_CONFIG_KEYS`, or gives invalid settings. The message starts with
     * `controllers::AssistantBot:` and names the key.
     * @throws std::runtime_error If another `Update()` is still saving.
     * @throws drogon::orm::DrogonDbException If saving fails.
     */
    auto BotSettings::Update(Json::Value overrides) -> drogon::Task<> {
        auto next{ build(m_defaults, overrides) };
        {
            std::lock_guard lock{ m_mutex };
            // Two saves in flight could reach the database and memory in different orders.
            if (m_saving) {
                throw std::runtime_error{ "controllers::AssistantBot: another save of the settings is still running" };
            }
            m_saving = true;
        }
        try {
            co_await storage::save_setting(m_db, BOT_SETTINGS_NAME, overrides);
        } catch (...) {
            std::lock_guard lock{ m_mutex };
            m_saving = false;
            throw;
        }
        std::lock_guard lock{ m_mutex };
        m_overrides = std::move(overrides);
        m_current = std::move(next);
        m_saving = false;
    }

} // namespace workflow
