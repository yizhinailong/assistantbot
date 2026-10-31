/**
 * @file http.cpp
 * @brief Keep configured integration traffic asynchronous and observable without header leaks.
 */
#include "tools/http.hpp"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cmath>
#include <regex>
#include <stdexcept>

#include <drogon/drogon.h>

#include "admin/http_traces.hpp"

namespace tools {
    /**
     * @brief Send one request without retrying side effects and record bounded diagnostics.
     * @param url HTTP(S) URL without user information or fragment.
     * @param method GET, POST, PUT, PATCH, or DELETE.
     * @param body Owned request text, at most 1 MiB.
     * @param headers String-valued request headers.
     * @param timeout Overall response timeout in seconds.
     * @param source Diagnostic source, mcp or tool.
     * @param session Conversation key, or empty for an admin probe.
     * @return Bounded response; throws on bad input, oversized output, or transport failure.
     */
    auto http_request(
        std::string url,
        std::string method,
        std::string body,
        Json::Value headers,
        double timeout,
        std::string source,
        std::string session
    ) -> drogon::Task<HttpResult> {
        static std::regex const URL{ R"((https?://[^\s/@?#]+)(/[^\s#]*)?)" };
        std::smatch match{};
        if (!std::regex_match(url, match, URL) || body.size() > 1024 * 1024 || !std::isfinite(timeout) || timeout <= 0 || timeout > 300 || !headers.isObject()) {
            throw std::invalid_argument{ "invalid integration HTTP request" };
        }
        drogon::HttpMethod verb{};
        if (method == "GET") {
            verb = drogon::Get;
        } else if (method == "POST") {
            verb = drogon::Post;
        } else if (method == "PUT") {
            verb = drogon::Put;
        } else if (method == "PATCH") {
            verb = drogon::Patch;
        } else if (method == "DELETE") {
            verb = drogon::Delete;
        } else {
            throw std::invalid_argument{ "unsupported HTTP method" };
        }
        auto request{ drogon::HttpRequest::newHttpRequest() };
        request->setMethod(verb);
        request->setPath(match[2].str().empty() ? "/" : match[2].str());
        request->setPathEncode(false);
        request->setBody(body);
        for (auto const& key : headers.getMemberNames()) {
            if (!headers[key].isString() || key.find_first_of("\r\n:") != std::string::npos || headers[key].asString().find_first_of("\r\n") != std::string::npos) {
                throw std::invalid_argument{ "invalid integration HTTP header" };
            }
            auto normalized{ key };
            std::ranges::transform(normalized, normalized.begin(), [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });
            static std::regex const HEADER{ R"([!#$%&'*+.^_`|~A-Za-z0-9-]+)" };
            if (!std::regex_match(key, HEADER) || normalized == "host" || normalized == "content-length" || normalized == "transfer-encoding" || normalized == "connection") {
                throw std::invalid_argument{ "invalid integration HTTP header" };
            }
            if (normalized == "content-type") {
                request->setContentTypeString(headers[key].asString());
            } else {
                request->addHeader(key, headers[key].asString());
            }
        }
        auto* loop{ trantor::EventLoop::getEventLoopOfCurrentThread() };
        auto client{ drogon::HttpClient::newHttpClient(match[1].str(), loop ? loop : drogon::app().getLoop()) };
        auto const started{ std::chrono::steady_clock::now() };
        drogon::HttpResponsePtr response{};
        std::string error{};
        try {
            response = co_await client->sendRequestCoro(request, timeout);
        } catch (std::exception const& exception) {
            error = exception.what();
        }
        Json::Value trace{};
        trace["source"] = source;
        trace["session"] = session;
        trace["method"] = method;
        trace["url"] = url;
        trace["status"] = response ? static_cast<int>(response->statusCode()) : 0;
        trace["elapsed_ms"] = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - started).count();
        trace["request_body"] = body.substr(0, 65536);
        trace["request_truncated"] = body.size() > 65536;
        trace["response_body"] = response ? std::string{ response->body().substr(0, 65536) } : "";
        trace["response_truncated"] = response && response->body().size() > 65536;
        trace["error"] = error;
        admin::record_http_trace(std::move(trace));
        if (!response) {
            throw std::runtime_error{ "integration HTTP request failed: " + error };
        }
        if (response->body().size() > 1024 * 1024) {
            throw std::runtime_error{ "integration response exceeds 1 MiB" };
        }
        HttpResult result{ .status = static_cast<int>(response->statusCode()), .body = std::string{ response->body() }, .headers = Json::Value{ Json::objectValue } };
        for (auto const& [key, value] : response->headers()) {
            result.headers[key] = value;
        }
        co_return result;
    }
} // namespace tools
