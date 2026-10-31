/**
 * @file reply_settings.hpp
 * @brief Declare authenticated reply settings endpoints.
 */
#pragma once

#include <string>

#include <drogon/HttpController.h>
#include <drogon/utils/coroutine.h>

namespace controllers {

    /**
     * @brief Serve authenticated reply settings administration.
     */
    class ReplySettingsController : public drogon::HttpController<ReplySettingsController> {
    public:
        METHOD_LIST_BEGIN
        ADD_METHOD_TO(ReplySettingsController::Settings, "/api/admin/bot-config", drogon::Get, drogon::Put);
        METHOD_LIST_END

        /**
         * @brief Read effective reply settings or persist a validated override object.
         * @param req Authenticated administrative request.
         * @return JSON result or a structured HTTP error.
         */
        auto Settings(drogon::HttpRequestPtr req) const -> drogon::Task<drogon::HttpResponsePtr>;
    };

} // namespace controllers
