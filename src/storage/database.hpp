/**
 * @file database.hpp
 * @brief Declare the plugin that opens the bot's SQLite database.
 */
#pragma once

#include <filesystem>

#include <drogon/orm/DbClient.h>
#include <drogon/plugins/Plugin.h>
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
     * the one that runs `initAndStart`.
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
     * Register it in config.json before the plugins that use it, and list it under their
     * `dependencies`, so that the database is ready when they start.
     *
     * | Key | Required | Meaning |
     * | --- | --- | --- |
     * | `data_dir` | No | Non-empty string, default `data`: directory of `DATABASE_FILE`, created when missing |
     */
    class Database : public drogon::Plugin<Database> {
    private:
        drogon::orm::DbClientPtr m_client; ///< Client from `open_database()`.

    public:
        /**
         * @brief Read `data_dir`, then open and migrate the database there.
         *
         * Drogon starts the plugins on the thread that calls `app().run()`, before any event
         * loop runs, so blocking on the database here is safe.
         * @param config The plugin's `config` object; null when config.json gives none.
         * @throws std::invalid_argument If `config` is not an object, or `data_dir` is not a
         * non-empty string.
         * @throws std::filesystem::filesystem_error If the directory cannot be created.
         * @throws std::runtime_error If SQLite cannot open the file, or the migration fails.
         */
        auto initAndStart(Json::Value const& config) -> void override;

        /**
         * @brief Drop the plugin's reference to the client.
         *
         * The database closes once no other holder of the client is left.
         */
        auto shutdown() -> void override;

        /**
         * @brief Get the client of the database.
         * @return The client; null before `initAndStart` and after `shutdown`.
         */
        [[nodiscard]] auto Client() const -> drogon::orm::DbClientPtr;
    };

} // namespace storage
