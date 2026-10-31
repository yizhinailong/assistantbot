/**
 * @file usage_stats.hpp
 * @brief Declare authenticated usage stats endpoints.
 */
#pragma once

#include <string>

#include <drogon/HttpController.h>
#include <drogon/utils/coroutine.h>

namespace controllers {

    /**
     * @brief Serve authenticated usage stats administration.
     */
    class UsageController : public drogon::HttpController<UsageController> {
    public:
        METHOD_LIST_BEGIN
        ADD_METHOD_TO(UsageController::Usage, "/api/admin/usage", drogon::Get);
        METHOD_LIST_END

        /**
         * @brief Query interval totals, UTC daily usage, and friend rankings.
         * @param req Authenticated administrative request.
         * @return JSON result or a structured HTTP error.
         */
        auto Usage(drogon::HttpRequestPtr req) const -> drogon::Task<drogon::HttpResponsePtr>;
    };

} // namespace controllers
