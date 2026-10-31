/**
 * @file runtime.hpp
 * @brief Declare runtime status administration endpoints.
 */
#pragma once

#include <drogon/HttpController.h>
#include <drogon/utils/coroutine.h>

namespace controllers {

    /**
     * @brief Serve runtime status administration endpoints under `/api/admin`.
     */
    class RuntimeController : public drogon::HttpController<RuntimeController> {
    public:
        METHOD_LIST_BEGIN
        ADD_METHOD_TO(RuntimeController::Status, "/api/admin/status", drogon::Get);
        ADD_METHOD_TO(RuntimeController::GetBotStatus, "/api/admin/bot-status", drogon::Get);
        ADD_METHOD_TO(RuntimeController::SetBotStatus, "/api/admin/bot-status", drogon::Post);
        ADD_METHOD_TO(RuntimeController::ProcessMetrics, "/api/admin/process-metrics", drogon::Get);
        METHOD_LIST_END

        /**
         * @brief Return the combined runtime status.
         * @param req Incoming request.
         * @return JSON status response.
         */
        auto Status(drogon::HttpRequestPtr req) const -> drogon::Task<drogon::HttpResponsePtr>;

        /**
         * @brief Return the bot pause flag.
         * @param req Incoming request.
         * @return JSON pause response.
         */
        auto GetBotStatus(
            drogon::HttpRequestPtr req
        ) const -> drogon::Task<drogon::HttpResponsePtr>;

        /**
         * @brief Validate and update the bot pause flag.
         * @param req Incoming JSON request.
         * @return Updated pause response.
         */
        auto SetBotStatus(
            drogon::HttpRequestPtr req
        ) const -> drogon::Task<drogon::HttpResponsePtr>;

        /**
         * @brief Return retained process samples.
         * @param req Incoming request.
         * @return JSON samples response.
         */
        auto ProcessMetrics(
            drogon::HttpRequestPtr req
        ) const -> drogon::Task<drogon::HttpResponsePtr>;
    };

} // namespace controllers
