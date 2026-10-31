/**
 * @file sessions.cpp
 * @brief Implement authenticated private conversation inspection and cursor history.
 */
#include "controllers/admin/sessions.hpp"

#include <algorithm>
#include <map>
#include <vector>

#include "admin/http_helpers.hpp"
#include "controllers/onebot/assistant_bot.hpp"
#include "storage/database.hpp"
#include "storage/messages.hpp"
#include "workflow/access_control.hpp"
#include "workflow/private_chat.hpp"

namespace controllers {

    namespace {

        /**
         * @brief Merge persistent peer identity with current in-memory metadata.
         * @param peer Persistent or empty identity record.
         * @param summary In-memory summary, when available.
         * @return Session list entry.
         */
        auto summary_json(
            Json::Value peer,
            std::optional<workflow::SessionSummary> summary
        ) -> Json::Value {
            peer["turns"] = 0;
            peer["busy"] = false;
            if (summary) {
                peer["user_id"] = std::to_string(summary->user_id);
                if (!summary->nickname.empty() || !peer.isMember("nickname")) {
                    peer["nickname"] = summary->nickname;
                }
                peer["last_active"] = Json::Int64{ std::max(peer.get("last_active", Json::Int64{ 0 }).asInt64(), summary->last_active) };
                peer["turns"] = Json::UInt64{ summary->turns };
                peer["busy"] = summary->busy;
            }
            return peer;
        }

        /**
         * @brief Read one conversation's persistent identity and memory context.
         * @param user_id Friend QQ number.
         * @return Detail object, or null when the friend has never been observed.
         */
        auto session_detail(std::int64_t user_id) -> drogon::Task<Json::Value> {
            auto const chat{ drogon::app().getPlugin<AssistantBot>()->Chat() };
            auto const persisted{ co_await storage::message_peers(drogon::app().getPlugin<storage::Database>()->Client(), user_id) };
            auto const memory{ chat->Session(user_id) };
            if (!memory && persisted.empty()) {
                co_return Json::Value{};
            }
            auto body{ summary_json(persisted.empty() ? Json::Value{ Json::objectValue } : persisted[0], memory ? std::optional{ memory->summary } : std::nullopt) };
            body["history"] = Json::Value{ Json::arrayValue };
            if (memory) {
                for (auto const& turn : memory->history) {
                    Json::Value value{ Json::objectValue };
                    value["user"] = turn.user;
                    value["assistant"] = turn.assistant;
                    body["history"].append(std::move(value));
                }
            }
            co_return body;
        }

    } // namespace

    /**
     * @brief List active and persisted private conversations.
     * @param req Authenticated administrative request.
     * @return JSON result or a structured HTTP error.
     */
    auto SessionsController::List(
        drogon::HttpRequestPtr /*req*/
    ) const -> drogon::Task<drogon::HttpResponsePtr> {
        try {
            auto const peers{ co_await storage::message_peers(drogon::app().getPlugin<storage::Database>()->Client()) };
            std::map<std::int64_t, Json::Value> by_user{};
            for (auto const& peer : peers) {
                by_user[workflow::parse_conversation(peer["user_id"].asString())] = summary_json(peer, std::nullopt);
            }
            for (auto const& summary : drogon::app().getPlugin<AssistantBot>()->Chat()->Sessions()) {
                auto& peer{ by_user[summary.user_id] };
                if (peer.isNull()) {
                    peer = Json::Value{ Json::objectValue };
                }
                peer = summary_json(std::move(peer), summary);
            }
            std::vector<Json::Value> sorted{};
            for (auto& [id, value] : by_user) {
                sorted.push_back(std::move(value));
            }
            std::ranges::sort(sorted, [](auto const& left, auto const& right) { return left["last_active"].asInt64() > right["last_active"].asInt64(); });
            Json::Value body{ Json::objectValue };
            body["sessions"] = Json::Value{ Json::arrayValue };
            for (auto& value : sorted) {
                body["sessions"].append(std::move(value));
            }
            co_return drogon::HttpResponse::newHttpJsonResponse(body);
        } catch (std::exception const& error) {
            LOG_ERROR << "Reading sessions failed: " << error.what();
            co_return admin::json_error("could not read sessions", drogon::k500InternalServerError);
        }
    }

    /**
     * @brief Inspect retained in-memory turns without restoring stored messages.
     * @param req Authenticated administrative request.
     * @param qq Decimal friend QQ number from the route.
     * @return JSON result or a structured HTTP error.
     */
    auto SessionsController::Detail(
        drogon::HttpRequestPtr /*req*/,
        std::string qq
    ) const -> drogon::Task<drogon::HttpResponsePtr> {
        try {
            auto body{ co_await session_detail(workflow::parse_conversation(qq)) };
            if (body.isNull()) {
                co_return admin::json_error("session not found", drogon::k404NotFound);
            }
            co_return drogon::HttpResponse::newHttpJsonResponse(body);
        } catch (std::invalid_argument const& error) {
            co_return admin::json_error(error.what(), drogon::k400BadRequest);
        } catch (std::exception const& error) {
            LOG_ERROR << "Reading session detail failed: " << error.what();
            co_return admin::json_error("could not read session", drogon::k500InternalServerError);
        }
    }

    /**
     * @brief Reset memory context and pending prompts without deleting the message log.
     * @param req Authenticated administrative request.
     * @param qq Decimal friend QQ number from the route.
     * @return JSON result or a structured HTTP error.
     */
    auto SessionsController::Reset(
        drogon::HttpRequestPtr /*req*/,
        std::string qq
    ) const -> drogon::Task<drogon::HttpResponsePtr> {
        try {
            auto const id{ workflow::parse_conversation(qq) };
            if ((co_await session_detail(id)).isNull()) {
                co_return admin::json_error("session not found", drogon::k404NotFound);
            }
            drogon::app().getPlugin<AssistantBot>()->Chat()->Reset(id);
            co_return drogon::HttpResponse::newHttpJsonResponse(co_await session_detail(id));
        } catch (std::invalid_argument const& error) {
            co_return admin::json_error(error.what(), drogon::k400BadRequest);
        } catch (std::exception const& error) {
            LOG_ERROR << "Resetting session failed: " << error.what();
            co_return admin::json_error("could not reset session", drogon::k500InternalServerError);
        }
    }

    /**
     * @brief Read a bounded message page using stable decimal ID cursors.
     * @param req Authenticated administrative request.
     * @param qq Decimal friend QQ number from the route.
     * @return JSON result or a structured HTTP error.
     */
    auto SessionsController::Messages(
        drogon::HttpRequestPtr req,
        std::string qq
    ) const -> drogon::Task<drogon::HttpResponsePtr> {
        try {
            auto const id{ workflow::parse_conversation(qq) };
            auto const limit{ admin::query_integer(req, "limit", 50, 1, 200) };
            std::optional<std::int64_t> before{};
            std::optional<std::int64_t> after{};
            if (req->getParameters().contains("before")) {
                before = admin::query_integer(req, "before", 0, 1);
            }
            if (req->getParameters().contains("after")) {
                after = admin::query_integer(req, "after", 0);
            }
            if (before && after) {
                throw std::invalid_argument{ "before and after cannot be combined" };
            }
            co_return drogon::HttpResponse::newHttpJsonResponse(co_await storage::message_page(drogon::app().getPlugin<storage::Database>()->Client(), id, limit, before, after));
        } catch (std::invalid_argument const& error) {
            co_return admin::json_error(error.what(), drogon::k400BadRequest);
        } catch (std::exception const& error) {
            LOG_ERROR << "Reading messages failed: " << error.what();
            co_return admin::json_error("could not read messages", drogon::k500InternalServerError);
        }
    }

} // namespace controllers
