/**
 * @file feature_records.cpp
 * @brief Apply document compare-and-swap updates in individual SQLite statements.
 */
#include "storage/feature_records.hpp"

#include <memory>
#include <stdexcept>

#include <json/json.h>

#include "common/time.hpp"
#include "storage/query.hpp"

namespace storage {

    namespace {

        /**
         * @brief Decode one stored feature document without exposing malformed JSON.
         * @param row Stored row.
         * @return API document with a lossless string revision.
         */
        auto record_json(drogon::orm::Row const& row) -> Json::Value {
            Json::Value result{};
            auto const text{ row["value"].as<std::string>() };
            std::unique_ptr<Json::CharReader> reader{ Json::CharReaderBuilder{}.newCharReader() };
            std::string errors{};
            if (!reader->parse(text.data(), text.data() + text.size(), &result["value"], &errors)) {
                throw std::runtime_error{ "stored feature document is not valid JSON" };
            }
            result["key"] = row["key"].as<std::string>();
            result["revision"] = std::to_string(row["revision"].as<std::int64_t>());
            result["updated_at"] = Json::Int64{ row["updated_at"].as<std::int64_t>() };
            return result;
        }

    } // namespace

    /**
     * @brief Read one feature document or every document of a kind.
     * @param db Database client.
     * @param kind Record category, bound as a SQL value.
     * @param key Optional document identifier.
     * @return Array of key, value, revision, and update timestamp objects.
     */
    auto feature_records(
        drogon::orm::DbClientPtr db,
        std::string kind,
        std::optional<std::string> key
    ) -> drogon::Task<Json::Value> {
        auto const rows{ co_await query(db, "SELECT key, value, revision, updated_at FROM feature_records WHERE kind = ? AND (? IS NULL OR key = ?) ORDER BY key", kind, key, key) };
        Json::Value result{ Json::arrayValue };
        for (auto const& row : rows) {
            result.append(record_json(row));
        }
        co_return result;
    }

    /**
     * @brief Save a document with optional optimistic concurrency checking.
     * @param db Database client.
     * @param kind Record category.
     * @param key Document identifier.
     * @param value JSON document.
     * @param revision Expected revision; zero requires a new record.
     * @return Saved document with its incremented revision.
     * @throws std::runtime_error If the expected revision is stale or the record was deleted.
     */
    auto save_feature_record(
        drogon::orm::DbClientPtr db,
        std::string kind,
        std::string key,
        Json::Value value,
        std::optional<std::int64_t> revision
    ) -> drogon::Task<Json::Value> {
        if (revision && *revision < 0) {
            throw std::invalid_argument{ "revision must be nonnegative" };
        }
        Json::StreamWriterBuilder writer{};
        writer["indentation"] = "";
        auto const rows{ co_await query(db, "INSERT INTO feature_records (kind, key, value, revision, updated_at) SELECT ?, ?, ?, 1, ? WHERE ? IS NULL OR ? = 0 OR EXISTS (SELECT 1 FROM feature_records WHERE kind = ? AND key = ? AND revision = ?) ON CONFLICT (kind, key) DO UPDATE SET value = excluded.value, revision = feature_records.revision + 1, updated_at = excluded.updated_at WHERE ? IS NULL OR feature_records.revision = ? RETURNING *", kind, key, Json::writeString(writer, value), common::unix_seconds(), revision, revision, kind, key, revision, revision, revision) };
        if (rows.empty()) {
            throw std::runtime_error{ "document changed; refresh before saving" };
        }
        co_return record_json(rows.at(0));
    }

    /**
     * @brief Delete a document only when the optional expected revision matches.
     * @param db Database client.
     * @param kind Record category.
     * @param key Document identifier.
     * @param revision Expected revision, or empty for an unconditional delete.
     * @return Whether a record was removed.
     */
    auto delete_feature_record(
        drogon::orm::DbClientPtr db,
        std::string kind,
        std::string key,
        std::optional<std::int64_t> revision
    ) -> drogon::Task<bool> {
        auto const rows{ co_await query(db, "DELETE FROM feature_records WHERE kind = ? AND key = ? AND (? IS NULL OR revision = ?) RETURNING key", kind, key, revision, revision) };
        co_return !rows.empty();
    }

} // namespace storage
