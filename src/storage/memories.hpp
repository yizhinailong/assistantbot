/**
 * @file memories.hpp
 * @brief Persist versioned long-term memory entries and their embedding spaces.
 */
#pragma once
#include <optional>
#include <string>

#include <drogon/orm/DbClient.h>
#include <drogon/utils/coroutine.h>
#include <json/value.h>

namespace storage {
    /**
     * @brief Read bounded memory entries for one conversation or all conversations.
     * @param db Database client.
     * @param session Optional positive friend or negative group key.
     * @param limit Maximum entries, at most 2000.
     * @return Entries in descending ID order, including private vector metadata.
     */
    auto memories(
        drogon::orm::DbClientPtr db,
        std::optional<std::int64_t> session,
        int limit = 200
    ) -> drogon::Task<Json::Value>;
    /**
     * @brief Insert or revise one memory while preserving optimistic revisions.
     * @param db Database client.
     * @param session Conversation key.
     * @param content Bounded memory text.
     * @param embedding Finite vector or empty JSON array.
     * @param space Endpoint/model identity associated with the vector.
     * @param id Existing ID or empty to insert.
     * @param revision Expected revision for an update.
     * @return Saved entry, or a conflict exception.
     */
    auto save_memory(
        drogon::orm::DbClientPtr db,
        std::int64_t session,
        std::string content,
        Json::Value embedding,
        std::string space,
        std::optional<std::int64_t> id = {},
        std::int64_t revision = 0
    ) -> drogon::Task<Json::Value>;
    /**
     * @brief Delete one memory restricted to a conversation and revision when supplied.
     * @param db Database client.
     * @param id Memory identifier.
     * @param session Optional conversation restriction for model tools.
     * @param revision Optional expected revision.
     * @return Whether an entry was deleted.
     */
    auto delete_memory(
        drogon::orm::DbClientPtr db,
        std::int64_t id,
        std::optional<std::int64_t> session = {},
        std::optional<std::int64_t> revision = {}
    ) -> drogon::Task<bool>;
} // namespace storage
