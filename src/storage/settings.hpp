/**
 * @file settings.hpp
 * @brief Declare reading and saving the settings rows of the bot's SQLite database.
 */
#pragma once

#include <optional>
#include <string>

#include <drogon/orm/DbClient.h>
#include <drogon/utils/coroutine.h>
#include <json/value.h>

namespace storage {

    /**
     * @brief Read a setting saved with `save_setting()`.
     *
     * Blocks until the database answers: call it only on a thread that runs no event loop,
     * such as the one that runs `initAndStart`.
     * @param db Client of the database.
     * @param name Name of the setting.
     * @return The saved value, or empty when none is saved.
     * @throws drogon::orm::DrogonDbException If the statement fails.
     * @throws std::runtime_error If the saved text is not valid JSON.
     */
    auto load_setting(
        drogon::orm::DbClient& db,
        std::string const& name
    ) -> std::optional<Json::Value>;

    /**
     * @brief Save a setting, replacing any value saved under its name.
     * @param db Client of the database.
     * @param name Name of the setting.
     * @param value Value to save, stored as compact JSON text.
     * @return Task that ends when the row is written.
     * @throws drogon::orm::DrogonDbException If the statement fails.
     */
    auto save_setting(
        drogon::orm::DbClientPtr db,
        std::string name,
        Json::Value value
    ) -> drogon::Task<>;

} // namespace storage
