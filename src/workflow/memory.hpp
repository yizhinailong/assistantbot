/**
 * @file memory.hpp
 * @brief Enrich conversations with editable aliases, person profiles, and persistent memories.
 */
#pragma once
#include <mutex>
#include <unordered_map>

#include "workflow/feature_settings.hpp"
#include "workflow/model_roles.hpp"
#include "workflow/private_chat.hpp"

namespace workflow {
    /**
     * @brief Validate an editable alias or global person document.
     * @param kind aliases or people.
     * @param key Conversation key for aliases, positive QQ for people.
     * @param value Full document to normalize.
     * @return Complete validated document.
     */
    auto validate_memory_document(
        std::string const& kind,
        std::string const& key,
        Json::Value value
    ) -> Json::Value;

    /**
     * @brief Persist and retrieve memory without restoring the short-term session store.
     */
    class MemoryService {
    private:
        drogon::orm::DbClientPtr m_database;                               ///< Persistent memory database.
        std::shared_ptr<ModelRoles> m_models;                              ///< Role-based inference and embeddings.
        std::mutex m_mutex;                                                ///< Guards maintenance counters and claims.
        std::unordered_map<std::string, std::vector<Json::Value>> m_turns; ///< Bounded turns awaiting extraction, keyed by conversation and sender.
        std::unordered_map<std::string, bool> m_running;                   ///< One maintenance call per conversation.

    public:
        /**
         * @brief Bind persistent memory to the shared model router.
         * @param database Database client.
         * @param models Model role service.
         */
        MemoryService(drogon::orm::DbClientPtr database, std::shared_ptr<ModelRoles> models);
        /**
         * @brief Add or revise a memory with a fresh portable embedding when enabled.
         * @param snapshot Complete feature settings.
         * @param session Conversation key.
         * @param content Memory text.
         * @param id Existing entry ID for revision.
         * @param revision Expected existing revision.
         * @return Saved memory document.
         */
        auto Save(
            Json::Value snapshot,
            std::int64_t session,
            std::string content,
            std::optional<std::int64_t> id = {},
            std::int64_t revision = 0
        ) -> drogon::Task<Json::Value>;
        /**
         * @brief Recall matching memory text using cosine similarity or lexical fallback.
         * @param snapshot Complete feature settings.
         * @param session Conversation key.
         * @param query Search text.
         * @return Bounded memory documents without embedding vectors.
         */
        auto Recall(
            Json::Value snapshot,
            std::int64_t session,
            std::string query
        ) -> drogon::Task<Json::Value>;
        /**
         * @brief Build bounded alias, person, affinity, and recalled memory context.
         * @param snapshot Complete feature settings.
         * @param peer Actual sender and recipient.
         * @param prompt Current message.
         * @return JSON context for an explicitly labelled reference-only system message.
         */
        auto Context(
            Json::Value snapshot,
            Peer peer,
            std::string prompt
        ) -> drogon::Task<Json::Value>;
        /**
         * @brief Extract durable facts after the configured number of successful replies.
         * @param snapshot Complete feature settings.
         * @param peer Actual sender and conversation.
         * @param prompt User text from this turn.
         * @param reply Assistant text from this turn.
         * @return Task completing after bounded maintenance or an intentional skip.
         */
        auto Maintain(
            Json::Value snapshot,
            Peer peer,
            std::string prompt,
            std::string reply
        ) -> drogon::Task<>;
    };
} // namespace workflow
