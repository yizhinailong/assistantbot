/**
 * @file messages.cpp
 * @brief Append private messages and query stable cursor pages and conversation peers.
 */
#include "storage/messages.hpp"

#include <algorithm>
#include <vector>

#include "storage/query.hpp"

namespace storage {

    namespace {

        /**
         * @brief Serialize one persisted message.
         * @param row SQLite message row.
         * @return JSON with precision-preserving identifiers.
         */
        auto message_json(drogon::orm::Row const& row) -> Json::Value {
            Json::Value body{ Json::objectValue };
            body["id"] = std::to_string(row["id"].as<std::int64_t>());
            body["timestamp"] = Json::Int64{ row["timestamp"].as<std::int64_t>() };
            body["user_id"] = std::to_string(row["user_id"].as<std::int64_t>());
            body["sender_id"] = std::to_string(row["sender_id"].as<std::int64_t>());
            body["group_id"] = std::to_string(row["group_id"].as<std::int64_t>());
            for (auto const* key : { "nickname", "direction", "content", "status", "error" }) {
                body[key] = row[key].as<std::string>();
            }
            return body;
        }

    } // namespace

    /**
     * @brief Append a message and return its immutable stored representation.
     * @param db Database client.
     * @param message Message to save.
     * @return JSON with a decimal string cursor ID.
     * @throws drogon::orm::DrogonDbException If saving fails.
     */
    auto append_message(
        drogon::orm::DbClientPtr db,
        ChatMessage message
    ) -> drogon::Task<Json::Value> {
        auto const saved{ co_await query(db, "INSERT INTO chat_messages (timestamp, user_id, nickname, direction, content, status, error, sender_id, group_id) VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?) RETURNING *", message.timestamp, message.user_id, message.nickname, message.direction, message.content, message.status, message.error, message.sender_id, message.group_id) };
        co_return message_json(saved.at(0));
    }

    /**
     * @brief Query a bounded chronological message page using stable ID cursors.
     * @param db Database client.
     * @param user_id Friend's QQ number.
     * @param limit Maximum records to return.
     * @param before Exclusive upper ID for older history; empty reads latest records.
     * @param after Exclusive lower ID for reconnect catch-up; mutually exclusive with before.
     * @return Messages, has_more, and cursor IDs as strings.
     * @throws drogon::orm::DrogonDbException If querying fails.
     */
    auto message_page(
        drogon::orm::DbClientPtr db,
        std::int64_t user_id,
        std::int64_t limit,
        std::optional<std::int64_t> before,
        std::optional<std::int64_t> after
    ) -> drogon::Task<Json::Value> {
        auto const rows{ after ? co_await query(db, "SELECT * FROM chat_messages WHERE user_id = ? AND id > ? ORDER BY id ASC LIMIT ?", user_id, *after, limit + 1) : co_await query(db, "SELECT * FROM chat_messages WHERE user_id = ? AND id < ? ORDER BY id DESC LIMIT ?", user_id, before.value_or(INT64_MAX), limit + 1) };
        std::vector<Json::Value> messages{};
        for (auto const& row : rows) {
            if (messages.size() == static_cast<std::size_t>(limit)) {
                break;
            }
            messages.push_back(message_json(row));
        }
        if (!after) {
            std::ranges::reverse(messages);
        }
        Json::Value body{ Json::objectValue };
        body["messages"] = Json::Value{ Json::arrayValue };
        for (auto& message : messages) {
            body["messages"].append(std::move(message));
        }
        body["has_more"] = rows.size() > static_cast<std::size_t>(limit);
        body["next_before"] = body["messages"].empty() ? Json::Value{} : body["messages"][0]["id"];
        body["next_after"] = body["messages"].empty() ? Json::Value{} : body["messages"][body["messages"].size() - 1]["id"];
        co_return body;
    }

    /**
     * @brief Read persistent conversation identities, including after a restart.
     * @param db Database client.
     * @param user_id Optional friend filter.
     * @return Array of user_id, nickname, and last_active records.
     * @throws drogon::orm::DrogonDbException If querying fails.
     */
    auto message_peers(
        drogon::orm::DbClientPtr db,
        std::optional<std::int64_t> user_id
    ) -> drogon::Task<Json::Value> {
        auto const rows{ co_await query(db, "SELECT user_id, nickname, timestamp AS last_active FROM chat_messages WHERE id IN (SELECT max(id) FROM chat_messages WHERE (? IS NULL OR user_id = ?) GROUP BY user_id) ORDER BY timestamp DESC, user_id ASC", user_id, user_id) };
        Json::Value peers{ Json::arrayValue };
        for (auto const& row : rows) {
            Json::Value peer{ Json::objectValue };
            peer["user_id"] = std::to_string(row["user_id"].as<std::int64_t>());
            peer["nickname"] = row["nickname"].as<std::string>();
            peer["last_active"] = Json::Int64{ row["last_active"].as<std::int64_t>() };
            peers.append(std::move(peer));
        }
        co_return peers;
    }

} // namespace storage
