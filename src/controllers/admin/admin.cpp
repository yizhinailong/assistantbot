/**
 * @file admin.cpp
 * @brief Implement the administrative HTTP controller.
 */
#include "controllers/admin/admin.hpp"

#include "admin/auth.hpp"

namespace controllers {

    /**
     * @brief Exchange an administrator token for an opaque session cookie.
     * @param req JSON request containing the token.
     * @return Authentication result with a session cookie on success.
     */
    auto Admin::Login(drogon::HttpRequestPtr req) const -> drogon::Task<drogon::HttpResponsePtr> {
        auto const input{ req->getJsonObject() };
        Json::Value body{ Json::objectValue };
        if (input == nullptr || !input->isObject() || !(*input)["token"].isString() || (*input)["token"].asString().empty() || (*input)["token"].asString().size() > 4096) {
            body["error"] = "token must contain 1 to 4096 bytes";
            auto response{ drogon::HttpResponse::newHttpJsonResponse(body) };
            response->setStatusCode(drogon::k400BadRequest);
            co_return response;
        }
        auto* auth{ drogon::app().getPlugin<admin::Auth>() };
        auto const session{ auth->Login((*input)["token"].asString()) };
        if (session.empty()) {
            body["error"] = "invalid administrator token";
            auto response{ drogon::HttpResponse::newHttpJsonResponse(body) };
            response->setStatusCode(drogon::k401Unauthorized);
            co_return response;
        }
        auth->Logout(req->getCookie(admin::ADMIN_SESSION_COOKIE));
        body["authenticated"] = true;
        auto response{ drogon::HttpResponse::newHttpJsonResponse(body) };
        drogon::Cookie cookie{ admin::ADMIN_SESSION_COOKIE, session };
        cookie.setHttpOnly(true);
        cookie.setSameSite(drogon::Cookie::SameSite::kStrict);
        cookie.setPath("/api/admin");
        cookie.setMaxAge(12 * 60 * 60);
        response->addCookie(cookie);
        co_return response;
    }

    /**
     * @brief Report whether the request has a valid administrator session.
     * @param req Request carrying the session cookie.
     * @return JSON authentication state.
     */
    auto Admin::AuthStatus(
        drogon::HttpRequestPtr req
    ) const -> drogon::Task<drogon::HttpResponsePtr> {
        Json::Value body{ Json::objectValue };
        auto const* auth{ drogon::app().getPlugin<admin::Auth>() };
        body["authenticated"] = auth != nullptr && auth->Authenticated(req->getCookie(admin::ADMIN_SESSION_COOKIE));
        co_return drogon::HttpResponse::newHttpJsonResponse(body);
    }

    /**
     * @brief Revoke the current session and expire its cookie.
     * @param req Request carrying the session cookie.
     * @return JSON authentication state and an expired cookie.
     */
    auto Admin::Logout(drogon::HttpRequestPtr req) const -> drogon::Task<drogon::HttpResponsePtr> {
        drogon::app().getPlugin<admin::Auth>()->Logout(req->getCookie(admin::ADMIN_SESSION_COOKIE));
        Json::Value body{ Json::objectValue };
        body["authenticated"] = false;
        auto response{ drogon::HttpResponse::newHttpJsonResponse(body) };
        drogon::Cookie cookie{ admin::ADMIN_SESSION_COOKIE, "" };
        cookie.setHttpOnly(true);
        cookie.setSameSite(drogon::Cookie::SameSite::kStrict);
        cookie.setPath("/api/admin");
        cookie.setMaxAge(0);
        response->addCookie(cookie);
        co_return response;
    }

    /**
     * @brief Report that the service is running.
     * @param req Incoming request; unused.
     * @return JSON response `{"status": "ok"}`.
     */
    auto Admin::Health(
        drogon::HttpRequestPtr /*req*/
    ) const -> drogon::Task<drogon::HttpResponsePtr> {
        Json::Value body{};
        body["status"] = "ok";
        co_return drogon::HttpResponse::newHttpJsonResponse(body);
    }

} // namespace controllers
