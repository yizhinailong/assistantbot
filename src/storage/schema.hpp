/**
 * @file schema.hpp
 * @brief Declare the schema migrations of the bot's SQLite database.
 */
#pragma once

#include <cstdint>

#include <drogon/orm/DbClient.h>

namespace storage {

    /**
     * @brief Schema version that `migrate()` brings a database to: the number of migrations.
     * @return The latest version, at least 1.
     */
    auto latest_schema_version() -> std::int64_t;

    /**
     * @brief Bring a database to `latest_schema_version()`.
     *
     * `PRAGMA user_version` holds the number of migrations applied, so a new database
     * starts at 0 and runs them all. Each migration runs in its own transaction together
     * with the version update, so a failed one leaves the database at the version before
     * it. Blocks until done; the client must run its statements one at a time, as the one
     * from `open_database()` does, and nothing else may use it meanwhile.
     * @param db Client of the database.
     * @throws std::runtime_error If the database has a newer schema than this build knows,
     * or a statement fails. The message starts with `storage::Database:`.
     */
    auto migrate(drogon::orm::DbClient& db) -> void;

    /**
     * @brief Read `PRAGMA user_version`.
     * @param db Client of the database.
     * @return The schema version of the database; 0 for a new one.
     * @throws drogon::orm::DrogonDbException If the statement fails.
     */
    auto schema_version(drogon::orm::DbClient& db) -> std::int64_t;

} // namespace storage
