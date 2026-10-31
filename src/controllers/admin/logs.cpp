/**
 * @file logs.cpp
 * @brief Query buffered logs and update the runtime logging level.
 */
#include "controllers/admin/logs.hpp"

#include "admin/http_helpers.hpp"
#include "admin/live_events.hpp"
#include "workflow/access_control.hpp"

namespace controllers {

    /**
     * @brief Read the bounded process log ring with filters and a cursor.
     * @param req Authenticated administrative request.
     * @return JSON result or a structured HTTP error.
     */
    auto LogsController::Logs(
        drogon::HttpRequestPtr req
    ) const -> drogon::Task<drogon::HttpResponsePtr> {
        try {
            admin::LogFilter filter{ req->getParameter("level"), req->getParameter("keyword"), req->getParameter("qq") };
            admin::validate_log_filter(filter);
            if (!filter.qq.empty()) {
                filter.qq = std::to_string(workflow::parse_qq(filter.qq));
            }
            auto const limit{ admin::query_integer(req, "limit", 200, 1, 5000) };
            std::optional<std::int64_t> after{};
            if (req->getParameters().contains("after")) {
                after = admin::query_integer(req, "after", 0);
            }
            co_return drogon::HttpResponse::newHttpJsonResponse(admin::read_logs(filter, after, static_cast<std::size_t>(limit)));
        } catch (std::invalid_argument const& error) {
            co_return admin::json_error(error.what(), drogon::k400BadRequest);
        }
    }

    /**
     * @brief Validate and update the runtime logging threshold.
     * @param req Authenticated administrative request.
     * @return JSON result or a structured HTTP error.
     */
    auto LogsController::SetLevel(
        drogon::HttpRequestPtr req
    ) const -> drogon::Task<drogon::HttpResponsePtr> {
        try {
            auto const body{ req->getJsonObject() };
            if (body == nullptr || !body->isObject() || body->size() != 1 || !(*body)["level"].isString()) {
                throw std::invalid_argument{ "body must contain only a string level field" };
            }
            admin::set_log_level((*body)["level"].asString());
            co_return drogon::HttpResponse::newHttpJsonResponse(*body);
        } catch (std::invalid_argument const& error) {
            co_return admin::json_error(error.what(), drogon::k400BadRequest);
        }
    }

} // namespace controllers
