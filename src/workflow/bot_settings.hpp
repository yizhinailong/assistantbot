/**
 * @file bot_settings.hpp
 * @brief Declare the current settings of the bot: config.json overlaid with the saved overrides.
 */
#pragma once

#include <memory>
#include <mutex>

#include <drogon/orm/DbClient.h>
#include <drogon/utils/coroutine.h>
#include <json/value.h>

#include "workflow/bot_config.hpp"

namespace workflow {

    constexpr char const* BOT_SETTINGS_NAME{ "bot" }; ///< Name of the overrides in the database's settings table.

    /**
     * @brief Hold the bot's settings: the plugin config from config.json, overlaid with the
     * overrides saved in the database.
     *
     * Hands out immutable snapshots. A reply takes one when it starts and keeps it to the
     * end, so a saved change applies from the next reply on. Every method locks one mutex for
     * a short time, never across `co_await`. Share it through a `std::shared_ptr`, because
     * `Update()` uses `this` across `co_await`.
     */
    class BotSettings {
    private:
        Json::Value m_defaults;                     ///< Plugin config from config.json, as an object; never changes.
        drogon::orm::DbClientPtr m_db;              ///< Database that keeps the overrides.
        mutable std::mutex m_mutex;                 ///< Guards the members below.
        Json::Value m_overrides;                    ///< Saved overrides: an object of keys from `BOT_CONFIG_KEYS`.
        std::shared_ptr<BotConfig const> m_current; ///< Defaults overlaid with `m_overrides`.
        bool m_saving{ false };                     ///< Whether an `Update()` is writing to the database.

    public:
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
        BotSettings(Json::Value defaults, drogon::orm::DbClientPtr db);

        /**
         * @brief Get the settings in effect.
         * @return Snapshot that later updates leave unchanged.
         */
        [[nodiscard]] auto Current() const -> std::shared_ptr<BotConfig const>;

        /**
         * @brief Get the saved overrides.
         * @return Object of the keys that differ from config.json; empty when none.
         */
        [[nodiscard]] auto Overrides() const -> Json::Value;

        /**
         * @brief Describe the current values, saved overrides, and config.json defaults together.
         * @param fallback_model Model configured by the OpenAI plugin.
         * @return Administrative settings view without secrets.
         */
        [[nodiscard]] auto Describe(std::string const& fallback_model) const -> Json::Value;

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
        auto Update(Json::Value overrides) -> drogon::Task<>;
    };

} // namespace workflow
