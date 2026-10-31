/**
 * @file sessions.hpp
 * @brief Declare authenticated sessions endpoints.
 */
#pragma once

#include <string>

#include <drogon/HttpController.h>
#include <drogon/utils/coroutine.h>

namespace controllers {

    /**
     * @brief Serve authenticated sessions administration.
     */
    class SessionsController : public drogon::HttpController<SessionsController> {
    public:
        METHOD_LIST_BEGIN
        ADD_METHOD_TO(SessionsController::List, "/api/admin/sessions", drogon::Get);
        ADD_METHOD_TO(SessionsController::Detail, "/api/admin/sessions/{1}", drogon::Get);
        ADD_METHOD_TO(SessionsController::Reset, "/api/admin/sessions/{1}/reset", drogon::Post);
        ADD_METHOD_TO(SessionsController::Messages, "/api/admin/sessions/{1}/messages", drogon::Get);
        METHOD_LIST_END

        /**
         * @brief List active and persisted private conversations.
         * @param req Authenticated administrative request.
         * @return JSON result or a structured HTTP error.
         */
        auto List(drogon::HttpRequestPtr req) const -> drogon::Task<drogon::HttpResponsePtr>;

        /**
         * @brief Inspect retained in-memory turns without restoring stored messages.
         * @param req Authenticated administrative request.
         * @param qq Decimal friend QQ number from the route.
         * @return JSON result or a structured HTTP error.
         */
        auto Detail(
            drogon::HttpRequestPtr req,
            std::string qq
        ) const -> drogon::Task<drogon::HttpResponsePtr>;

        /**
         * @brief Reset memory context and pending prompts without deleting the message log.
         * @param req Authenticated administrative request.
         * @param qq Decimal friend QQ number from the route.
         * @return JSON result or a structured HTTP error.
         */
        auto Reset(
            drogon::HttpRequestPtr req,
            std::string qq
        ) const -> drogon::Task<drogon::HttpResponsePtr>;

        /**
         * @brief Read a bounded message page using stable decimal ID cursors.
         * @param req Authenticated administrative request.
         * @param qq Decimal friend QQ number from the route.
         * @return JSON result or a structured HTTP error.
         */
        auto Messages(
            drogon::HttpRequestPtr req,
            std::string qq
        ) const -> drogon::Task<drogon::HttpResponsePtr>;
    };

} // namespace controllers
