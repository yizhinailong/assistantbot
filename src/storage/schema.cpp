/**
 * @file schema.cpp
 * @brief Implement the schema migrations of the bot's SQLite database.
 */
#include "storage/schema.hpp"

#include <exception>
#include <format>
#include <span>
#include <stdexcept>
#include <string>
#include <vector>

namespace storage {

    namespace {

        /**
         * @brief Statements of one migration, run in order in one transaction.
         *
         * drogon's SQLite client prepares one statement per call, so each string holds one.
         */
        using Migration = std::vector<char const*>;

        /**
         * @brief Every migration, oldest first; migration `i` brings the schema to version `i + 1`.
         *
         * Append new migrations and never edit the published ones, because databases that
         * already ran them keep their version.
         * @return The migrations.
         */
        auto migrations() -> std::span<Migration const> {
            static std::vector<Migration> const list{
                // 1: settings saved by the admin pages; `value` is a JSON document.
                {
                    "CREATE TABLE settings (name TEXT PRIMARY KEY NOT NULL, value TEXT NOT NULL) STRICT",
                },
                // 2: append each completed model request for the overview's daily totals.
                {
                    "CREATE TABLE model_usage (id INTEGER PRIMARY KEY, timestamp INTEGER NOT NULL, user_id INTEGER NOT NULL, model TEXT NOT NULL, succeeded INTEGER NOT NULL CHECK (succeeded IN (0, 1)), prompt_tokens INTEGER, completion_tokens INTEGER, total_tokens INTEGER) STRICT",
                    "CREATE INDEX model_usage_timestamp ON model_usage (timestamp)",
                },
                // 3: retain every private friend message independently of model context.
                {
                    "CREATE TABLE chat_messages (id INTEGER PRIMARY KEY, timestamp INTEGER NOT NULL, user_id INTEGER NOT NULL, nickname TEXT NOT NULL, direction TEXT NOT NULL CHECK (direction IN ('incoming', 'outgoing')), content TEXT NOT NULL, status TEXT NOT NULL, error TEXT NOT NULL) STRICT",
                    "CREATE INDEX chat_messages_user_id_id ON chat_messages (user_id, id)",
                },
                // 4: keep vendor-neutral cache hit reports without rewriting old usage.
                {
                    "ALTER TABLE model_usage ADD COLUMN cached_tokens INTEGER",
                },
                // 5: editable agent documents use statement-level optimistic concurrency.
                {
                    "CREATE TABLE feature_records (kind TEXT NOT NULL, key TEXT NOT NULL, value TEXT NOT NULL, revision INTEGER NOT NULL CHECK (revision > 0), updated_at INTEGER NOT NULL, PRIMARY KEY (kind, key)) STRICT",
                },
                // 6: retain group recipients and actual senders while keeping context keys disjoint.
                {
                    "ALTER TABLE chat_messages ADD COLUMN sender_id INTEGER NOT NULL DEFAULT 0",
                    "ALTER TABLE chat_messages ADD COLUMN group_id INTEGER NOT NULL DEFAULT 0",
                    "UPDATE chat_messages SET sender_id = user_id",
                },
                // 7: portable vectors carry their endpoint/model space and document revisions.
                {
                    "CREATE TABLE memories (id INTEGER PRIMARY KEY AUTOINCREMENT, session_id INTEGER NOT NULL CHECK(session_id != 0), content TEXT NOT NULL, embedding TEXT NOT NULL, embedding_space TEXT NOT NULL, revision INTEGER NOT NULL, created_at INTEGER NOT NULL, updated_at INTEGER NOT NULL) STRICT",
                    "CREATE INDEX memories_session_id ON memories (session_id, id)",
                },
                // 8: UTC reminders survive restart and claim delivery at most once.
                {
                    "CREATE TABLE scheduled_tasks (id INTEGER PRIMARY KEY AUTOINCREMENT, session_id INTEGER NOT NULL, sender_id INTEGER NOT NULL, due_at INTEGER NOT NULL, content TEXT NOT NULL, daily INTEGER NOT NULL CHECK(daily IN (0,1)), status TEXT NOT NULL CHECK(status IN ('pending','running','done','failed','cancelled')), error TEXT NOT NULL, created_at INTEGER NOT NULL) STRICT",
                    "CREATE INDEX scheduled_tasks_due ON scheduled_tasks (status, due_at)",
                },
            };
            return list;
        }

        /**
         * @brief Run one migration and record its version, or roll it back.
         * @param db Client of the database, running one statement at a time.
         * @param migration Statements to run.
         * @param version Schema version after the migration.
         * @throws std::exception What the failing statement threw, after the rollback.
         */
        auto apply(
            drogon::orm::DbClient& db,
            Migration const& migration,
            std::int64_t version
        ) -> void {
            db.execSqlSync("BEGIN IMMEDIATE");
            try {
                for (auto const* statement : migration) {
                    db.execSqlSync(statement);
                }
                // PRAGMA takes no bound parameters.
                db.execSqlSync(std::format("PRAGMA user_version = {}", version));
                db.execSqlSync("COMMIT");
            } catch (...) {
                try {
                    db.execSqlSync("ROLLBACK");
                } catch (std::exception const&) {
                    // SQLite may have rolled back already; the first error is the one to report.
                }
                throw;
            }
        }

    } // namespace

    /**
     * @brief Schema version that `migrate()` brings a database to: the number of migrations.
     * @return The latest version, at least 1.
     */
    auto latest_schema_version() -> std::int64_t {
        return static_cast<std::int64_t>(migrations().size());
    }

    /**
     * @brief Read `PRAGMA user_version`.
     * @param db Client of the database.
     * @return The schema version of the database; 0 for a new one.
     * @throws drogon::orm::DrogonDbException If the statement fails.
     */
    auto schema_version(drogon::orm::DbClient& db) -> std::int64_t {
        auto const result{ db.execSqlSync("PRAGMA user_version") };
        return result.at(0)["user_version"].as<std::int64_t>();
    }

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
    auto migrate(drogon::orm::DbClient& db) -> void {
        std::int64_t current{};
        try {
            current = schema_version(db);
        } catch (std::exception const& error) {
            throw std::runtime_error{ std::format("storage::Database: reading the schema version failed: {}", error.what()) };
        }
        auto const latest{ latest_schema_version() };
        if (current > latest) {
            throw std::runtime_error{ std::format("storage::Database: the database has schema version {}, newer than the {} this build knows", current, latest) };
        }
        auto const list{ migrations() };
        for (auto version{ current }; version < latest; ++version) {
            try {
                apply(db, list[static_cast<std::size_t>(version)], version + 1);
            } catch (std::exception const& error) {
                throw std::runtime_error{ std::format("storage::Database: migrating to schema version {} failed: {}", version + 1, error.what()) };
            }
        }
    }

} // namespace storage
