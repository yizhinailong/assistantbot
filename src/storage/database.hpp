/**
 * @file database.hpp
 * @brief Declare the service that opens the bot's SQLite database.
 */
#pragma once

#include <filesystem>

#include <drogon/orm/DbClient.h>
#include <json/value.h>

namespace storage {

    constexpr char const* DEFAULT_DATA_DIR{ "data" };         ///< `data_dir` when the config sets none.
    constexpr char const* DATABASE_FILE{ "assistantbot.db" }; ///< Name of the database file in the data directory.

    /**
     * @brief Open the database in a data directory, creating both when missing, and migrate it.
     *
     * The client has one connection, so its statements run one at a time in the order they
     * are sent, on a thread of its own, so it needs none of the app's event loops. Blocks
     * until the migration ends: call it only on a thread that runs no event loop, such as
     * the one that runs `app::Application::initAndStart`.
     * @param data_dir Directory of `DATABASE_FILE`; a relative path resolves against the
     * working directory.
     * @return Client of the migrated database.
     * @throws std::filesystem::filesystem_error If the directory cannot be created.
     * @throws std::runtime_error If SQLite cannot open the file, or the migration fails.
     */
    auto open_database(std::filesystem::path const& data_dir) -> drogon::orm::DbClientPtr;

    /**
     * @brief Open and migrate the bot's SQLite database, and share its client.
     *
     * `app::Application` starts this service before the bot and runtime services.
     *
     * | Key | Required | Meaning |
     * | --- | --- | --- |
     * | `data_dir` | No | Non-empty string, default `data`: directory of `DATABASE_FILE`, created when missing |
     */
    class Database {
    private:
        drogon::orm::DbClientPtr m_client; ///< Client from `open_database()`.

    public:
        /**
         * @brief Read `data_dir`, then open and migrate the database there.
         *
         * `app::Application` calls this before any event loop runs, so blocking is safe.
         * @param config Application configuration containing an optional `data_dir`.
         * @throws std::invalid_argument If `config` is not an object, or `data_dir` is not a
         * non-empty string.
         * @throws std::filesystem::filesystem_error If the directory cannot be created.
         * @throws std::runtime_error If SQLite cannot open the file, or the migration fails.
         */
        auto Start(Json::Value const& config) -> void;

        /**
         * @brief Drop the service's reference to the client.
         *
         * The database closes once no other holder of the client is left.
         */
        auto Stop() -> void;

        /**
         * @brief Get the client of the database.
         * @return The client; null before `Start` and after `Stop`.
         */
        [[nodiscard]] auto Client() const -> drogon::orm::DbClientPtr;
    };

} // namespace storage
