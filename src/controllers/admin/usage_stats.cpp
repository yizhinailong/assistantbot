/**
 * @file usage_stats.cpp
 * @brief Query consistent UTC usage totals and grouped statistics.
 */
#include "controllers/admin/usage_stats.hpp"

#include "admin/http_helpers.hpp"
#include "common/time.hpp"
#include "storage/database.hpp"
#include "storage/usage.hpp"

namespace controllers {

    /**
     * @brief Query interval totals, UTC daily usage, and friend rankings.
     * @param req Authenticated administrative request.
     * @return JSON result or a structured HTTP error.
     */
    auto UsageController::Usage(
        drogon::HttpRequestPtr req
    ) const -> drogon::Task<drogon::HttpResponsePtr> {
        try {
            auto const today{ common::unix_seconds() / 86400 * 86400 };
            auto const from{ admin::query_integer(req, "from", today - 29 * 86400, 0, 253402214400LL) };
            auto const to{ admin::query_integer(req, "to", today + 86400, 1, 253402300800LL) };
            if (to <= from || to - from > 366 * 86400LL) {
                throw std::invalid_argument{ "usage interval must be positive and no longer than 366 days; to is exclusive" };
            }
            co_return drogon::HttpResponse::newHttpJsonResponse(co_await storage::usage_report(drogon::app().getPlugin<storage::Database>()->Client(), from, to, today));
        } catch (std::invalid_argument const& error) {
            co_return admin::json_error(error.what(), drogon::k400BadRequest);
        } catch (std::exception const& error) {
            LOG_ERROR << "Reading usage statistics failed: " << error.what();
            co_return admin::json_error("could not read usage statistics", drogon::k500InternalServerError);
        }
    }

} // namespace controllers
