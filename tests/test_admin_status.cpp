/**
 * @file test_admin_status.cpp
 * @brief Check authenticated runtime status, bot pause, and process metrics endpoints.
 */
#include <atomic>
#include <chrono>
#include <cstdint>
#include <stdexcept>
#include <string>

#include <drogon/drogon.h>
#include <drogon/drogon_test.h>

#include "support.hpp"

namespace {

    std::atomic_bool g_fail_status{}; ///< Fail the status action while other actions still work.
    std::atomic_bool g_fail_all{};    ///< Fail all upstream actions.

    /**
     * @brief Serve typed OneBot status actions, with controllable partial failure.
     */
    auto serve_onebot() -> void {
        drogon::app().registerHandler(
            "/napcat/{1}",
            [](drogon::HttpRequestPtr, std::string action) -> drogon::Task<drogon::HttpResponsePtr> {
                if (g_fail_all || (g_fail_status && action == "get_status")) {
                    co_return drogon::HttpResponse::newHttpResponse(drogon::k503ServiceUnavailable, drogon::CT_TEXT_PLAIN);
                }
                Json::Value body{ Json::objectValue };
                body["status"] = "ok";
                body["retcode"] = 0;
                auto& data{ body["data"] };
                if (action == "get_login_info") {
                    data["user_id"] = Json::Int64{ 30001 };
                    data["nickname"] = "test bot";
                } else if (action == "get_status") {
                    data["online"] = true;
                    data["good"] = true;
                } else if (action == "get_version_info") {
                    data["app_name"] = "NapCat.Onebot";
                    data["app_version"] = "test-version";
                    data["protocol_version"] = "v11";
                }
                co_return drogon::HttpResponse::newHttpJsonResponse(body);
            },
            { drogon::Post }
        );
    }

    /**
     * @brief Send a request with an optional administrative session.
     * @param path Absolute request path.
     * @param method HTTP method.
     * @param body JSON body; null sends none.
     * @param cookie Session value; empty sends none.
     * @return HTTP response.
     * @throws std::runtime_error If sending fails.
     */

    auto request_admin(
        char const* path,
        drogon::HttpMethod method = drogon::Get,
        Json::Value const& body = Json::Value{},
        std::string const& cookie = {}
    ) -> drogon::HttpResponsePtr {
        auto request{ body.isNull() ? drogon::HttpRequest::newHttpRequest() : drogon::HttpRequest::newHttpJsonRequest(body) };
        request->setMethod(method);
        request->setPath(path);
        if (!cookie.empty()) {
            request->addCookie("assistantbot_admin_session", cookie);
        }
        auto const client{ drogon::HttpClient::newHttpClient(test_support::SERVER_URL) };
        auto const [result, response]{ client->sendRequest(request, 5.0) };
        if (result != drogon::ReqResult::Ok) {
            throw std::runtime_error{ "administrative request failed" };
        }
        return response;
    }

    /**
     * @brief Read a response JSON object.
     * @param response HTTP response.
     * @return Parsed JSON.
     * @throws std::runtime_error If the body is not JSON.
     */
    auto json_body(drogon::HttpResponsePtr const& response) -> Json::Value {
        auto const body{ response->getJsonObject() };
        if (body == nullptr) {
            throw std::runtime_error{ "response was not JSON" };
        }
        return *body;
    }

} // namespace

DROGON_TEST(RuntimeEndpointsRequireAuthentication) {
    for (auto const* path : { "/api/admin/status", "/api/admin/bot-status", "/api/admin/process-metrics" }) {
        CHECK(request_admin(path)->statusCode() == drogon::k401Unauthorized);
    }
}

DROGON_TEST(StatusReportsRuntimeAndOneBotData) {
    auto const response{ request_admin("/api/admin/status", drogon::Get, {}, test_support::login_admin()) };
    REQUIRE(response->statusCode() == drogon::k200OK);
    auto const& body{ json_body(response) };
    CHECK(body["version"].isString());
    CHECK(body["version"].asString() == ASSISTANTBOT_VERSION);
    CHECK(body["started_at"].isInt64());
    auto const now{ std::chrono::duration_cast<std::chrono::seconds>(std::chrono::system_clock::now().time_since_epoch()).count() };
    CHECK(body["started_at"].asInt64() <= now);
    CHECK(body["model"].asString() == "fake-model");
    CHECK(body["session_count"].isUInt64());
    CHECK(body["bot"]["user_id"].asString() == "30001");
    CHECK(body["bot"]["nickname"].asString() == "test bot");
    CHECK(body["napcat"].isObject());
    CHECK(body["napcat"]["online"].asBool());
    CHECK(body["napcat"]["good"].asBool());
    CHECK(body["napcat"]["app_name"].asString() == "NapCat.Onebot");
    CHECK(body["napcat"]["app_version"].asString() == "test-version");
    CHECK(body["napcat"]["protocol_version"].asString() == "v11");
    CHECK(body["today_usage"]["requests"].asInt64() == 0);
    CHECK(body["today_usage"]["total_tokens"].asInt64() == 0);
    CHECK(body["today_usage"]["timezone"].asString() == "UTC");
    CHECK(!body["paused"].asBool());
}

DROGON_TEST(StatusPreservesOtherFieldsOnUpstreamFailure) {
    auto const cookie{ test_support::login_admin() };
    g_fail_status = true;
    auto const partial{ json_body(request_admin("/api/admin/status", drogon::Get, {}, cookie)) };
    g_fail_status = false;
    CHECK(partial["napcat"]["online"].isNull());
    CHECK(partial["napcat"]["good"].isNull());
    CHECK(partial["napcat"]["app_version"].asString() == "test-version");
    CHECK(partial["bot"]["nickname"].asString() == "test bot");
    g_fail_all = true;
    auto const failed{ json_body(request_admin("/api/admin/status", drogon::Get, {}, cookie)) };
    g_fail_all = false;
    CHECK(failed["bot"].isNull());
    CHECK(failed["napcat"]["online"].isNull());
    CHECK(failed["napcat"]["app_version"].isNull());
    CHECK(failed["model"].asString() == "fake-model");
    CHECK(failed["today_usage"]["requests"].asInt64() == 0);
}

DROGON_TEST(BotStatusValidatesAndUpdatesPause) {
    auto const cookie{ test_support::login_admin() };
    auto const initial{ request_admin("/api/admin/bot-status", drogon::Get, {}, cookie) };
    REQUIRE(initial->statusCode() == drogon::k200OK);
    CHECK(!json_body(initial)["paused"].asBool());

    for (auto const& invalid : { Json::Value{ Json::arrayValue }, Json::Value{ Json::objectValue } }) {
        CHECK(request_admin("/api/admin/bot-status", drogon::Post, invalid, cookie)->statusCode() == drogon::k400BadRequest);
    }
    Json::Value wrong{ Json::objectValue };
    wrong["paused"] = "yes";
    CHECK(request_admin("/api/admin/bot-status", drogon::Post, wrong, cookie)->statusCode() == drogon::k400BadRequest);

    Json::Value update{ Json::objectValue };
    update["paused"] = true;
    auto const paused{ request_admin("/api/admin/bot-status", drogon::Post, update, cookie) };
    REQUIRE(paused->statusCode() == drogon::k200OK);
    CHECK(json_body(paused)["paused"].asBool());
    update["paused"] = false;
    CHECK(request_admin("/api/admin/bot-status", drogon::Post, update, cookie)->statusCode() == drogon::k200OK);
}

DROGON_TEST(ProcessMetricsIncludesStartupSample) {
    auto const response{ request_admin("/api/admin/process-metrics", drogon::Get, {}, test_support::login_admin()) };
    REQUIRE(response->statusCode() == drogon::k200OK);
    auto const& body{ json_body(response) };
    CHECK(body["interval_seconds"].asInt() == 60);
    CHECK(body["capacity"].asUInt64() == 1440);
    REQUIRE(body["samples"].isArray());
    REQUIRE(!body["samples"].empty());
    auto const& sample{ body["samples"][0] };
    CHECK(sample["timestamp"].isInt64());
    CHECK(sample.isMember("rss_bytes"));
    CHECK(sample.isMember("peak_rss_bytes"));
    CHECK(sample.isMember("threads"));
    auto const valid_rss_bytes{ sample["rss_bytes"].isNull() || sample["rss_bytes"].isUInt64() };
    CHECK(valid_rss_bytes);
#if defined(__linux__) || defined(_WIN32)
    REQUIRE(sample["rss_bytes"].isUInt64());
    CHECK(sample["rss_bytes"].asUInt64() > 0);
#endif
    auto const valid_peak_rss_bytes{ sample["peak_rss_bytes"].isNull() || sample["peak_rss_bytes"].isUInt64() };
    CHECK(valid_peak_rss_bytes);
#if defined(__linux__) || defined(_WIN32)
    REQUIRE(sample["peak_rss_bytes"].isUInt64());
    CHECK(sample["peak_rss_bytes"].asUInt64() > 0);
#endif
    auto const valid_threads{ sample["threads"].isNull() || sample["threads"].isUInt64() };
    CHECK(valid_threads);
#if defined(__linux__) || defined(_WIN32)
    REQUIRE(sample["threads"].isUInt64());
    CHECK(sample["threads"].asUInt64() > 0);
#endif
}

auto main(int argc, char** argv) -> int {
    test_support::load_config();
    serve_onebot();
    return test_support::run_tests_with_app(argc, argv);
}
