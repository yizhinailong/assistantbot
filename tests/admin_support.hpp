/**
 * @file admin_support.hpp
 * @brief Share authenticated HTTP requests for administration integration tests.
 */
#pragma once

#include <optional>
#include <stdexcept>
#include <string>
#include <utility>

#include <drogon/drogon.h>

#include "admin/auth.hpp"
#include "support.hpp"

namespace test_support {

    /**
     * @brief Send an administrative request with an optional session.
     * @param path Absolute URL path, optionally including a query string.
     * @param method HTTP method.
     * @param body Optional JSON body.
     * @param session Cookie value; nullopt signs in, an empty string omits authentication.
     * @return HTTP response.
     * @throws std::runtime_error If sending fails.
     */
    inline auto request_admin(
        std::string const& path,
        drogon::HttpMethod method = drogon::Get,
        Json::Value const& body = {},
        std::optional<std::string> session = std::nullopt
    ) -> drogon::HttpResponsePtr {
        auto request{ body.isNull() ? drogon::HttpRequest::newHttpRequest() : drogon::HttpRequest::newHttpJsonRequest(body) };
        request->setPath(path);
        request->setMethod(method);
        auto const cookie{ session ? *session : login_admin() };
        if (!cookie.empty()) {
            request->addCookie(admin::ADMIN_SESSION_COOKIE, cookie);
        }
        auto const client{ drogon::HttpClient::newHttpClient(SERVER_URL) };
        auto const [result, response]{ client->sendRequest(request, 5.0) };
        if (result != drogon::ReqResult::Ok) {
            throw std::runtime_error{ "administrative request failed" };
        }
        return response;
    }

    /**
     * @brief Extract a successful JSON response.
     * @param response HTTP response.
     * @return JSON body.
     * @throws std::runtime_error If the response was unsuccessful or not JSON.
     */
    inline auto success(drogon::HttpResponsePtr const& response) -> Json::Value {
        if (response->statusCode() != drogon::k200OK || response->getJsonObject() == nullptr) {
            throw std::runtime_error{ "expected successful JSON response: " + std::string{ response->body() } };
        }
        return *response->getJsonObject();
    }

    /**
     * @brief Build a one-field object.
     * @param key Field name.
     * @param value Field value.
     * @return Request body.
     */
    inline auto field(char const* key, Json::Value value) -> Json::Value {
        Json::Value body{ Json::objectValue };
        body[key] = std::move(value);
        return body;
    }

} // namespace test_support
