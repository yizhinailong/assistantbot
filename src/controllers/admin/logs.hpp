/**
 * @file logs.hpp
 * @brief Declare authenticated logs endpoints.
 */
#pragma once

#include <string>

#include <drogon/HttpController.h>
#include <drogon/utils/coroutine.h>

namespace controllers {

    /**
     * @brief Serve authenticated logs administration.
     */
    class LogsController : public drogon::HttpController<LogsController> {
    public:
        METHOD_LIST_BEGIN
        ADD_METHOD_TO(LogsController::Logs, "/api/admin/logs", drogon::Get);
        ADD_METHOD_TO(LogsController::SetLevel, "/api/admin/log-level", drogon::Put);
        METHOD_LIST_END

        /**
         * @brief Read the bounded process log ring with filters and a cursor.
         * @param req Authenticated administrative request.
         * @return JSON result or a structured HTTP error.
         */
        auto Logs(drogon::HttpRequestPtr req) const -> drogon::Task<drogon::HttpResponsePtr>;

        /**
         * @brief Validate and update the runtime logging threshold.
         * @param req Authenticated administrative request.
         * @return JSON result or a structured HTTP error.
         */
        auto SetLevel(drogon::HttpRequestPtr req) const -> drogon::Task<drogon::HttpResponsePtr>;
    };

} // namespace controllers
