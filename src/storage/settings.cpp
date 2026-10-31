/**
 * @file settings.cpp
 * @brief Implement reading and saving the settings rows of the bot's SQLite database.
 */
#include "storage/settings.hpp"

#include <format>
#include <memory>
#include <stdexcept>

#include <json/json.h>

#include "storage/query.hpp"

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
    ) -> std::optional<Json::Value> {
        auto const result{ db.execSqlSync("SELECT value FROM settings WHERE name = ?", name) };
        if (result.empty()) {
            return std::nullopt;
        }
        auto const text{ result.at(0)["value"].as<std::string>() };
        Json::Value value{};
        std::string errors{};
        std::unique_ptr<Json::CharReader> const reader{ Json::CharReaderBuilder{}.newCharReader() };
        if (!reader->parse(text.data(), text.data() + text.size(), &value, &errors)) {
            throw std::runtime_error{ std::format("storage::Database: setting {} is not valid JSON: {}", name, errors) };
        }
        return value;
    }

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
    ) -> drogon::Task<> {
        Json::StreamWriterBuilder builder{};
        builder["indentation"] = "";
        auto const text{ Json::writeString(builder, value) };
        co_await query(db, "INSERT INTO settings (name, value) VALUES (?, ?) ON CONFLICT (name) DO UPDATE SET value = excluded.value", name, text);
    }

} // namespace storage
