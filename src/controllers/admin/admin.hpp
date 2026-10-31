/**
 * @file admin.hpp
 * @brief Declare the administrative HTTP controller.
 */
#pragma once

#include <drogon/HttpController.h>
#include <drogon/utils/coroutine.h>

namespace controllers {

    /**
     * @brief Serve administrative endpoints under `/api/admin`.
     */
    class Admin : public drogon::HttpController<Admin> {
    public:
        METHOD_LIST_BEGIN
        ADD_METHOD_TO(Admin::Health, "/api/admin/health", drogon::Get);
        ADD_METHOD_TO(Admin::Login, "/api/admin/auth/login", drogon::Post);
        ADD_METHOD_TO(Admin::AuthStatus, "/api/admin/auth/status", drogon::Get);
        ADD_METHOD_TO(Admin::Logout, "/api/admin/auth/logout", drogon::Post);
        METHOD_LIST_END

        /**
         * @brief Exchange an administrator token for an opaque session cookie.
         * @param req JSON request containing the token.
         * @return Authentication result with a session cookie on success.
         */
        auto Login(drogon::HttpRequestPtr req) const -> drogon::Task<drogon::HttpResponsePtr>;

        /**
         * @brief Report whether the request has a valid administrator session.
         * @param req Request carrying the session cookie.
         * @return JSON authentication state.
         */
        auto AuthStatus(drogon::HttpRequestPtr req) const -> drogon::Task<drogon::HttpResponsePtr>;

        /**
         * @brief Revoke the current session and expire its cookie.
         * @param req Request carrying the session cookie.
         * @return JSON authentication state and an expired cookie.
         */
        auto Logout(drogon::HttpRequestPtr req) const -> drogon::Task<drogon::HttpResponsePtr>;

        /**
         * @brief Report that the service is running.
         * @param req Incoming request; unused.
         * @return JSON response `{"status": "ok"}`.
         */
        auto Health(drogon::HttpRequestPtr req) const -> drogon::Task<drogon::HttpResponsePtr>;
    };

} // namespace controllers
