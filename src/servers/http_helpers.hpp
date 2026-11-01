/**
 * @file http_helpers.hpp
 * @brief Share strict administrative JSON and query validation.
 */
#pragma once

#include <charconv>
#include <cstdint>
#include <stdexcept>
#include <string>
#include <string_view>

#include <drogon/drogon.h>

namespace admin {

    /**
     * @brief Build a JSON error using the shared API format.
     * @param message Public error message.
     * @param status HTTP status code.
     * @return Error response.
     */
    inline auto json_error(
        std::string const& message,
        drogon::HttpStatusCode status
    ) -> drogon::HttpResponsePtr {
        Json::Value body{ Json::objectValue };
        body["error"] = message;
        auto response{ drogon::HttpResponse::newHttpJsonResponse(body) };
        response->setStatusCode(status);
        return response;
    }

    /**
     * @brief Parse an unsigned decimal query value within signed 64-bit bounds.
     * @param value Digits with no whitespace or sign.
     * @param minimum Smallest permitted integer.
     * @param maximum Largest permitted integer.
     * @return Parsed integer.
     * @throws std::invalid_argument If the value is malformed or out of range.
     */
    inline auto decimal(
        std::string_view value,
        std::int64_t minimum = 0,
        std::int64_t maximum = INT64_MAX
    ) -> std::int64_t {
        if (value.empty()) {
            throw std::invalid_argument{ "expected a decimal integer in the permitted range" };
        }
        std::int64_t parsed{};
        auto const [end, error]{ std::from_chars(value.data(), value.data() + value.size(), parsed) };
        if (value.front() < '0' || value.front() > '9' || error != std::errc{} || end != value.data() + value.size() || parsed < minimum || parsed > maximum) {
            throw std::invalid_argument{ "expected a decimal integer in the permitted range" };
        }
        return parsed;
    }

    /**
     * @brief Read an optional integer query parameter, rejecting empty explicit values.
     * @param req Incoming request.
     * @param key Query parameter name.
     * @param fallback Value when absent.
     * @param minimum Smallest permitted value.
     * @param maximum Largest permitted value.
     * @return Validated query value.
     * @throws std::invalid_argument If the parameter is malformed or out of range.
     */
    inline auto query_integer(
        drogon::HttpRequestPtr const& req,
        std::string const& key,
        std::int64_t fallback,
        std::int64_t minimum = 0,
        std::int64_t maximum = INT64_MAX
    ) -> std::int64_t {
        auto const& parameters{ req->getParameters() };
        auto const found{ parameters.find(key) };
        return found == parameters.end() ? fallback : decimal(found->second, minimum, maximum);
    }

} // namespace admin
