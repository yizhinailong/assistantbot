/**
 * @file memory.hpp
 * @brief Declare alias, global person, affinity, and long-term memory endpoints.
 */
#pragma once
#include <drogon/HttpController.h>
#include <drogon/utils/coroutine.h>

namespace controllers {
    /**
     * @brief Manage versioned persistent memories separately from model context turns.
     */
    class MemoryController : public drogon::HttpController<MemoryController> {
    public:
        METHOD_LIST_BEGIN
        ADD_METHOD_TO(MemoryController::Documents, "/api/admin/memory-documents/{1}", drogon::Get);
        ADD_METHOD_TO(MemoryController::Document, "/api/admin/memory-documents/{1}/{2}", drogon::Get, drogon::Put, drogon::Delete);
        ADD_METHOD_TO(MemoryController::Memories, "/api/admin/memories", drogon::Get, drogon::Post);
        ADD_METHOD_TO(MemoryController::Entry, "/api/admin/memories/{1}", drogon::Put, drogon::Delete);
        ADD_METHOD_TO(MemoryController::Search, "/api/admin/memories/search", drogon::Post);
        METHOD_LIST_END
        /**
         * @brief List versioned aliases or global people.
         * @param req Authenticated request.
         * @param kind aliases or people.
         * @return Document records or validation error.
         */
        auto Documents(
            drogon::HttpRequestPtr req,
            std::string kind
        ) const -> drogon::Task<drogon::HttpResponsePtr>;
        /**
         * @brief Read, replace, or delete one validated document using optimistic revisions.
         * @param req Authenticated request.
         * @param kind aliases or people.
         * @param key Conversation or QQ key.
         * @return Saved or requested document, or HTTP error.
         */
        auto Document(
            drogon::HttpRequestPtr req,
            std::string kind,
            std::string key
        ) const -> drogon::Task<drogon::HttpResponsePtr>;
        /**
         * @brief List or create long-term memory entries.
         * @param req Authenticated request, optionally filtered by session.
         * @return Entries or a created entry.
         */
        auto Memories(drogon::HttpRequestPtr req) const -> drogon::Task<drogon::HttpResponsePtr>;
        /**
         * @brief Revise or delete one memory with its expected revision.
         * @param req Authenticated request.
         * @param id Positive memory ID.
         * @return Updated entry or deletion result.
         */
        auto Entry(
            drogon::HttpRequestPtr req,
            std::string id
        ) const -> drogon::Task<drogon::HttpResponsePtr>;
        /**
         * @brief Search one conversation's long-term memories.
         * @param req Authenticated request with session and query.
         * @return Bounded similarity matches.
         */
        auto Search(drogon::HttpRequestPtr req) const -> drogon::Task<drogon::HttpResponsePtr>;
    };
} // namespace controllers
