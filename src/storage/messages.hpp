/**
 * @file messages.hpp
 * @brief Append private messages and query stable cursor pages and conversation peers.
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
     * @brief One observed incoming message or attempted outgoing message.
     */
    struct ChatMessage {
        std::int64_t timestamp{}; ///< UTC Unix seconds.
        std::int64_t user_id{};   ///< Friend's QQ number.
        std::string nickname;     ///< Last observed nickname.
        std::string direction;    ///< Incoming or outgoing.
        std::string content;      ///< Text or readable media placeholders.
        std::string status;       ///< Received, ignored, sent, or failed.
        std::string error;        ///< Ignore or failure reason; empty on success.
    };

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
    ) -> drogon::Task<Json::Value>;

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
        std::optional<std::int64_t> before = std::nullopt,
        std::optional<std::int64_t> after = std::nullopt
    ) -> drogon::Task<Json::Value>;

    /**
     * @brief Read persistent conversation identities, including after a restart.
     * @param db Database client.
     * @param user_id Optional friend filter.
     * @return Array of user_id, nickname, and last_active records.
     * @throws drogon::orm::DrogonDbException If querying fails.
     */
    auto message_peers(
        drogon::orm::DbClientPtr db,
        std::optional<std::int64_t> user_id = std::nullopt
    ) -> drogon::Task<Json::Value>;

} // namespace storage
