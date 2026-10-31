/**
 * @file tasks.cpp
 * @brief Validate UTC task requests and expose persistent outcomes.
 */
#include "controllers/admin/tasks.hpp"

#include "admin/http_helpers.hpp"
#include "controllers/onebot/assistant_bot.hpp"
#include "workflow/scheduled_tasks.hpp"

namespace controllers {
    /**
     * @brief List tasks by conversation or create one durable reminder.
     * @param req Authenticated request.
     * @return Task records or created task.
     */
    auto TasksController::Tasks(
        drogon::HttpRequestPtr req
    ) const -> drogon::Task<drogon::HttpResponsePtr> {
        try {
            auto const tasks{ drogon::app().getPlugin<AssistantBot>()->Tasks() };
            if (req->method() == drogon::Post) {
                auto const body{ req->getJsonObject() };
                if (!body || !(*body)["session"].isString() || !(*body)["due_at"].isInt64() || !(*body)["content"].isString() || (!(*body)["daily"].isNull() && !(*body)["daily"].isBool())) {
                    throw std::invalid_argument{ "expected session, UTC due_at, content, and optional daily flag" };
                }
                auto const session{ workflow::parse_conversation((*body)["session"].asString()) };
                workflow::Peer peer{ .user_id = session > 0 ? session : 0, .group_id = session < 0 ? -session : 0 };
                co_return drogon::HttpResponse::newHttpJsonResponse(co_await tasks->Create(peer, (*body)["due_at"].asInt64(), (*body)["content"].asString(), (*body).get("daily", false).asBool()));
            }
            std::optional<std::int64_t> session{};
            if (req->getParameters().contains("session")) {
                session = workflow::parse_conversation(req->getParameter("session"));
            }
            Json::Value result{};
            result["tasks"] = co_await tasks->List(session);
            co_return drogon::HttpResponse::newHttpJsonResponse(result);
        } catch (std::invalid_argument const& error) {
            co_return admin::json_error(error.what(), drogon::k400BadRequest);
        } catch (drogon::orm::DrogonDbException const& error) {
            LOG_ERROR << error.base().what();
            co_return admin::json_error("task storage failed", drogon::k500InternalServerError);
        } catch (std::runtime_error const& error) {
            co_return admin::json_error(error.what(), drogon::k409Conflict);
        }
    }

    /**
     * @brief Cancel a task that has not started delivery.
     * @param req Authenticated request.
     * @param id Positive task identifier.
     * @return Cancellation acknowledgement or conflict.
     */
    auto TasksController::Cancel(
        drogon::HttpRequestPtr,
        std::string id
    ) const -> drogon::Task<drogon::HttpResponsePtr> {
        try {
            if (!(co_await drogon::app().getPlugin<AssistantBot>()->Tasks()->Cancel(admin::decimal(id, 1)))) {
                co_return admin::json_error("task is absent or has already started", drogon::k409Conflict);
            }
            Json::Value result{};
            result["cancelled"] = true;
            co_return drogon::HttpResponse::newHttpJsonResponse(result);
        } catch (std::invalid_argument const& error) {
            co_return admin::json_error(error.what(), drogon::k400BadRequest);
        } catch (std::exception const& error) {
            LOG_ERROR << error.what();
            co_return admin::json_error("could not cancel task", drogon::k500InternalServerError);
        }
    }
} // namespace controllers
