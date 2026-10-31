/**
 * @file memories.cpp
 * @brief Apply bounded long-term memory queries and statement-level revision checks.
 */
#include "storage/memories.hpp"

#include <cmath>
#include <memory>
#include <stdexcept>

#include <json/json.h>

#include "common/time.hpp"
#include "storage/query.hpp"

namespace storage {
    namespace {
        /**
         * @brief Decode one memory row with precision-preserving identifiers.
         * @param row SQLite memory row.
         * @return Complete internal memory document.
         */
        auto memory_json(drogon::orm::Row const& row) -> Json::Value {
            Json::Value value{};
            for (auto const* key : { "id", "session_id", "revision" }) {
                value[key] = std::to_string(row[key].as<std::int64_t>());
            }
            for (auto const* key : { "content", "embedding_space" }) {
                value[key] = row[key].as<std::string>();
            }
            for (auto const* key : { "created_at", "updated_at" }) {
                value[key] = Json::Int64{ row[key].as<std::int64_t>() };
            }
            auto const encoded{ row["embedding"].as<std::string>() };
            std::unique_ptr<Json::CharReader> reader{ Json::CharReaderBuilder{}.newCharReader() };
            std::string error{};
            if (!reader->parse(encoded.data(), encoded.data() + encoded.size(), &value["embedding"], &error) || !value["embedding"].isArray()) {
                throw std::runtime_error{ "invalid stored memory vector" };
            }
            return value;
        }
    } // namespace

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
        int limit
    ) -> drogon::Task<Json::Value> {
        if (limit < 1 || limit > 2000) {
            throw std::invalid_argument{ "memory query limit must be in [1, 2000]" };
        }
        auto const rows{ co_await query(db, "SELECT * FROM memories WHERE (? IS NULL OR session_id = ?) ORDER BY id DESC LIMIT ?", session, session, limit) };
        Json::Value result{ Json::arrayValue };
        for (auto const& row : rows) {
            result.append(memory_json(row));
        }
        co_return result;
    }

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
        std::optional<std::int64_t> id,
        std::int64_t revision
    ) -> drogon::Task<Json::Value> {
        if (session == 0 || content.empty() || content.size() > 8192 || !embedding.isArray() || embedding.size() > 4096 || space.size() > 4096 || (id && revision < 1)) {
            throw std::invalid_argument{ "expected a nonzero conversation and memory text of 1..8192 bytes" };
        }
        for (auto const& number : embedding) {
            if (!number.isNumeric() || !std::isfinite(number.asDouble())) {
                throw std::invalid_argument{ "memory vectors must contain finite numbers" };
            }
        }
        Json::StreamWriterBuilder writer{};
        writer["indentation"] = "";
        auto const encoded{ Json::writeString(writer, embedding) };
        auto const now{ common::unix_seconds() };
        auto const rows{ id ? co_await query(db, "UPDATE memories SET content = ?, embedding = ?, embedding_space = ?, revision = revision + 1, updated_at = ? WHERE id = ? AND session_id = ? AND revision = ? RETURNING *", content, encoded, space, now, *id, session, revision) : co_await query(db, "INSERT INTO memories (session_id, content, embedding, embedding_space, revision, created_at, updated_at) SELECT ?, ?, ?, ?, 1, ?, ? WHERE (SELECT count(*) FROM memories WHERE session_id = ?) < 2000 RETURNING *", session, content, encoded, space, now, now, session) };
        if (rows.empty()) {
            throw std::runtime_error{ "memory changed or conversation memory limit reached; refresh before saving" };
        }
        co_return memory_json(rows[0]);
    }

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
        std::optional<std::int64_t> session,
        std::optional<std::int64_t> revision
    ) -> drogon::Task<bool> {
        auto const rows{ co_await query(db, "DELETE FROM memories WHERE id = ? AND (? IS NULL OR session_id = ?) AND (? IS NULL OR revision = ?) RETURNING id", id, session, session, revision, revision) };
        co_return !rows.empty();
    }
} // namespace storage
