/**
 * @file memory.cpp
 * @brief Validate memory administration and keep vectors out of public entry documents.
 */
#include "controllers/admin/memory.hpp"

#include "admin/http_helpers.hpp"
#include "controllers/onebot/assistant_bot.hpp"
#include "storage/database.hpp"
#include "storage/feature_records.hpp"
#include "storage/memories.hpp"
#include "workflow/access_control.hpp"
#include "workflow/memory.hpp"

namespace {
    /**
     * @brief Validate a memory document category and normalize its optional identifier.
     * @param kind aliases or people.
     * @param key Optional key to normalize.
     * @return Normalized key.
     */
    auto document_key(std::string const& kind, std::string const& key = "") -> std::string {
        if (kind != "aliases" && kind != "people") {
            throw std::invalid_argument{ "unknown memory document kind" };
        }
        return key.empty() ? "" : std::to_string(kind == "people" ? workflow::parse_qq(key) : workflow::parse_conversation(key));
    }

    /**
     * @brief Omit internal vector bytes from memory responses.
     * @param value Stored entry.
     * @return Public entry with vector dimensions.
     */
    auto public_memory(Json::Value value) -> Json::Value {
        value["dimensions"] = value["embedding"].size();
        value.removeMember("embedding");
        return value;
    }

    /**
     * @brief Validate memory text and conversation fields before model requests.
     * @param body Incoming JSON object.
     * @return Conversation key.
     */
    auto memory_input(Json::Value const& body) -> std::int64_t {
        if (!body.isObject() || !body["session"].isString() || !body["content"].isString() || body["content"].asString().empty() || body["content"].asString().size() > 8192) {
            throw std::invalid_argument{ "expected session and content of 1..8192 bytes" };
        }
        return workflow::parse_conversation(body["session"].asString());
    }

    /**
     * @brief Map a caught storage or model exception to a consistent HTTP response.
     * @param error Exception that crossed a memory operation.
     * @return Public HTTP error.
     */
    auto failure(std::exception const& error) -> drogon::HttpResponsePtr {
        if (dynamic_cast<std::invalid_argument const*>(&error)) {
            return admin::json_error(error.what(), drogon::k400BadRequest);
        }
        if (dynamic_cast<drogon::orm::DrogonDbException const*>(&error)) {
            LOG_ERROR << "Memory database failure: " << error.what();
            return admin::json_error("memory storage failed", drogon::k500InternalServerError);
        }
        return admin::json_error(error.what(), drogon::k409Conflict);
    }
} // namespace

namespace controllers {
    /**
     * @brief List versioned aliases or global people.
     * @param req Authenticated request.
     * @param kind aliases or people.
     * @return Document records or validation error.
     */
    auto MemoryController::Documents(
        drogon::HttpRequestPtr,
        std::string kind
    ) const -> drogon::Task<drogon::HttpResponsePtr> {
        try {
            static_cast<void>(document_key(kind));
            Json::Value body{};
            body["records"] = co_await storage::feature_records(drogon::app().getPlugin<storage::Database>()->Client(), kind);
            co_return drogon::HttpResponse::newHttpJsonResponse(body);
        } catch (std::exception const& error) {
            co_return failure(error);
        }
    }

    /**
     * @brief Read, replace, or delete one validated document using optimistic revisions.
     * @param req Authenticated request.
     * @param kind aliases or people.
     * @param key Conversation or QQ key.
     * @return Saved or requested document, or HTTP error.
     */
    auto MemoryController::Document(
        drogon::HttpRequestPtr req,
        std::string kind,
        std::string key
    ) const -> drogon::Task<drogon::HttpResponsePtr> {
        try {
            key = document_key(kind, key);
            auto const db{ drogon::app().getPlugin<storage::Database>()->Client() };
            if (req->method() == drogon::Put) {
                auto const body{ req->getJsonObject() };
                if (!body || !(*body)["revision"].isString()) {
                    throw std::invalid_argument{ "expected value and decimal revision" };
                }
                auto value{ workflow::validate_memory_document(kind, key, (*body)["value"]) };
                co_return drogon::HttpResponse::newHttpJsonResponse(co_await storage::save_feature_record(db, kind, key, std::move(value), admin::decimal((*body)["revision"].asString())));
            }
            if (req->method() == drogon::Delete) {
                auto const expected{ admin::query_integer(req, "revision", 0, 1) };
                if (!(co_await storage::delete_feature_record(db, kind, key, expected))) {
                    co_return admin::json_error("document changed; refresh before deleting", drogon::k409Conflict);
                }
            }
            auto const values{ co_await storage::feature_records(db, kind, key) };
            if (!values.empty()) {
                co_return drogon::HttpResponse::newHttpJsonResponse(values[0]);
            }
            Json::Value empty{};
            empty["key"] = key;
            empty["revision"] = "0";
            empty["value"] = Json::Value{};
            co_return drogon::HttpResponse::newHttpJsonResponse(empty);
        } catch (std::exception const& error) {
            co_return failure(error);
        }
    }

    /**
     * @brief List or create long-term memory entries.
     * @param req Authenticated request, optionally filtered by session.
     * @return Entries or a created entry.
     */
    auto MemoryController::Memories(
        drogon::HttpRequestPtr req
    ) const -> drogon::Task<drogon::HttpResponsePtr> {
        try {
            auto const plugin{ drogon::app().getPlugin<AssistantBot>() };
            if (req->method() == drogon::Post) {
                auto const body{ req->getJsonObject() };
                if (!body) {
                    throw std::invalid_argument{ "expected JSON request" };
                }
                auto const session{ memory_input(*body) };
                co_return drogon::HttpResponse::newHttpJsonResponse(public_memory(co_await plugin->Memory()->Save(*plugin->Features()->Current(), session, (*body)["content"].asString())));
            }
            std::optional<std::int64_t> session{};
            if (req->getParameters().contains("session")) {
                session = workflow::parse_conversation(req->getParameter("session"));
            }
            auto const entries{ co_await storage::memories(drogon::app().getPlugin<storage::Database>()->Client(), session, static_cast<int>(admin::query_integer(req, "limit", 200, 1, 2000))) };
            Json::Value body{};
            body["memories"] = Json::Value{ Json::arrayValue };
            for (auto const& entry : entries) {
                body["memories"].append(public_memory(entry));
            }
            co_return drogon::HttpResponse::newHttpJsonResponse(body);
        } catch (std::exception const& error) {
            co_return failure(error);
        }
    }

    /**
     * @brief Revise or delete one memory with its expected revision.
     * @param req Authenticated request.
     * @param id Positive memory ID.
     * @return Updated entry or deletion result.
     */
    auto MemoryController::Entry(
        drogon::HttpRequestPtr req,
        std::string id
    ) const -> drogon::Task<drogon::HttpResponsePtr> {
        try {
            auto const number{ admin::decimal(id, 1) };
            if (req->method() == drogon::Delete) {
                if (!(co_await storage::delete_memory(drogon::app().getPlugin<storage::Database>()->Client(), number, {}, admin::query_integer(req, "revision", 0, 1)))) {
                    co_return admin::json_error("memory changed; refresh before deleting", drogon::k409Conflict);
                }
                Json::Value body{};
                body["deleted"] = true;
                co_return drogon::HttpResponse::newHttpJsonResponse(body);
            }
            auto const body{ req->getJsonObject() };
            if (!body || !(*body)["revision"].isString()) {
                throw std::invalid_argument{ "expected session, content, and revision" };
            }
            auto const session{ memory_input(*body) };
            auto const plugin{ drogon::app().getPlugin<AssistantBot>() };
            co_return drogon::HttpResponse::newHttpJsonResponse(public_memory(co_await plugin->Memory()->Save(*plugin->Features()->Current(), session, (*body)["content"].asString(), number, admin::decimal((*body)["revision"].asString(), 1))));
        } catch (std::exception const& error) {
            co_return failure(error);
        }
    }

    /**
     * @brief Search one conversation's long-term memories.
     * @param req Authenticated request with session and query.
     * @return Bounded similarity matches.
     */
    auto MemoryController::Search(
        drogon::HttpRequestPtr req
    ) const -> drogon::Task<drogon::HttpResponsePtr> {
        try {
            auto const body{ req->getJsonObject() };
            if (!body || !(*body)["session"].isString() || !(*body)["query"].isString() || (*body)["query"].asString().empty() || (*body)["query"].asString().size() > 8192) {
                throw std::invalid_argument{ "expected session and bounded query" };
            }
            auto const plugin{ drogon::app().getPlugin<AssistantBot>() };
            Json::Value result{};
            result["memories"] = co_await plugin->Memory()->Recall(*plugin->Features()->Current(), workflow::parse_conversation((*body)["session"].asString()), (*body)["query"].asString());
            co_return drogon::HttpResponse::newHttpJsonResponse(result);
        } catch (std::exception const& error) {
            co_return failure(error);
        }
    }
} // namespace controllers
