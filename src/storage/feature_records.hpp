/**
 * @file feature_records.hpp
 * @brief Store versioned documents for editable agent capabilities.
 */
#pragma once

#include <cstdint>
#include <optional>
#include <string>

#include <drogon/orm/DbClient.h>
#include <drogon/utils/coroutine.h>
#include <json/value.h>

namespace storage {

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
        std::optional<std::string> key = {}
    ) -> drogon::Task<Json::Value>;

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
        std::optional<std::int64_t> revision = {}
    ) -> drogon::Task<Json::Value>;

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
        std::optional<std::int64_t> revision = {}
    ) -> drogon::Task<bool>;

} // namespace storage
