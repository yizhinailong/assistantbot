/**
 * @file database.cpp
 * @brief Implement the plugin that opens the bot's SQLite database.
 */
#include "storage/database.hpp"

#include <format>
#include <stdexcept>
#include <string>

#include <drogon/drogon.h>
#include <sqlite3.h>

#include "storage/schema.hpp"

namespace storage {

    namespace {

        /**
         * @brief Quote a value for a drogon connection string.
         *
         * drogon splits the string at spaces unless a value is quoted, and inside quotes a
         * backslash escapes the next character.
         * @param value Value to quote.
         * @return The value in single quotes, with `\` and `'` escaped.
         */
        auto quote(std::string const& value) -> std::string {
            std::string quoted{ "'" };
            for (auto const ch : value) {
                if (ch == '\\' || ch == '\'') {
                    quoted.push_back('\\');
                }
                quoted.push_back(ch);
            }
            quoted.push_back('\'');
            return quoted;
        }

        /**
         * @brief Open the database file once with SQLite itself, creating it when missing.
         *
         * drogon 1.9.13 only logs a file that SQLite cannot open, retries a second later, and
         * then aborts the process with `Resource deadlock avoided`. Opening it here first
         * turns that into an exception that names the file and the reason.
         * @param file Path of the database file.
         * @throws std::runtime_error If SQLite cannot open or create the file.
         */
        auto check_openable(std::filesystem::path const& file) -> void {
            sqlite3* db{ nullptr };
            auto const status{ sqlite3_open_v2(file.string().c_str(), &db, SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE, nullptr) };
            std::string const reason{ db != nullptr ? sqlite3_errmsg(db) : sqlite3_errstr(status) };
            sqlite3_close(db);
            if (status != SQLITE_OK) {
                throw std::runtime_error{ std::format("storage::Database: cannot open {}: {}", file.string(), reason) };
            }
        }

    } // namespace

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
    auto open_database(std::filesystem::path const& data_dir) -> drogon::orm::DbClientPtr {
        std::filesystem::create_directories(data_dir);
        auto const file{ data_dir / DATABASE_FILE };
        check_openable(file);
        auto const client{ drogon::orm::DbClient::newSqlite3Client("filename=" + quote(file.string()), 1) };
        migrate(*client);
        LOG_INFO << "Opened " << file.string() << " at schema version " << latest_schema_version();
        return client;
    }

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
    auto Database::initAndStart(Json::Value const& config) -> void {
        if (!config.isNull() && !config.isObject()) {
            throw std::invalid_argument{ "storage::Database: config must be an object" };
        }
        std::string data_dir{ DEFAULT_DATA_DIR };
        if (config.isMember("data_dir")) {
            auto const& value{ config["data_dir"] };
            if (!value.isString() || value.asString().empty()) {
                throw std::invalid_argument{ "storage::Database: data_dir must be a non-empty string" };
            }
            data_dir = value.asString();
        }
        m_client = open_database(data_dir);
    }

    /**
     * @brief Drop the plugin's reference to the client.
     *
     * The database closes once no other holder of the client is left.
     */
    auto Database::shutdown() -> void {
        m_client.reset();
    }

    /**
     * @brief Get the client of the database.
     * @return The client; null before `initAndStart` and after `shutdown`.
     */
    auto Database::Client() const -> drogon::orm::DbClientPtr {
        return m_client;
    }

} // namespace storage
