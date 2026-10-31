/**
 * @file test_admin_http_traces.cpp
 * @brief Verify request ring bounds, redaction, authentication, and JSON downloads.
 */
#include <drogon/drogon_test.h>

#include "admin/http_traces.hpp"
#include "admin_support.hpp"

DROGON_TEST(HttpTracesRequireAuthenticationAndValidateFilters) {
    for (auto const* path : { "/api/admin/http-traces", "/api/admin/http-traces/download" }) {
        CHECK(test_support::request_admin(path, drogon::Get, {}, "")->statusCode() == drogon::k401Unauthorized);
    }
    CHECK(test_support::request_admin("/api/admin/http-traces", drogon::Delete, {}, "")->statusCode() == drogon::k401Unauthorized);
    CHECK(test_support::request_admin("/api/admin/http-traces?source=unknown")->statusCode() == drogon::k400BadRequest);
}

DROGON_TEST(HttpTracesRetainOnlyFiftyRedactedRequestsAndDownloadTheSameSnapshot) {
    admin::clear_http_traces();
    for (int i{}; i < 55; ++i) {
        Json::Value trace{};
        trace["source"] = i % 2 == 0 ? "model" : "onebot";
        trace["url"] = "https://example.invalid/v1?token=secret&x=1";
        trace["method"] = "POST";
        trace["status"] = 200;
        trace["headers"]["Authorization"] = "Bearer hidden";
        trace["request_body"] = "{\"token\":\"hidden\",\"image\":\"data:image/png;base64,private-image\"}";
        trace["response_body"] = test_support::ADMIN_TOKEN;
        trace["error"] = "";
        admin::record_http_trace(std::move(trace));
    }
    auto const response{ test_support::request_admin("/api/admin/http-traces") };
    auto const body{ test_support::success(response) };
    REQUIRE(body["traces"].size() == 50);
    CHECK(body["capacity"] == 50);
    CHECK(!std::string{ response->body() }.contains("private-image"));
    CHECK(!std::string{ response->body() }.contains(test_support::ADMIN_TOKEN));
    CHECK(!std::string{ response->body() }.contains("token=secret"));
    CHECK(!body["traces"][0].isMember("headers"));
    auto const filtered{ test_support::success(test_support::request_admin("/api/admin/http-traces?source=model")) };
    CHECK(filtered["traces"].size() == 25);
    auto const download{ test_support::request_admin("/api/admin/http-traces/download") };
    CHECK(download->getHeader("content-disposition").contains("attachment"));
    CHECK(test_support::success(download) == body);
    CHECK(test_support::success(test_support::request_admin("/api/admin/http-traces", drogon::Delete))["traces"].empty());
}

/**
 * @brief Run request diagnostic tests on the shared authenticated app fixture.
 * @param argc Argument count.
 * @param argv Argument vector.
 * @return Test status.
 */
auto main(int argc, char** argv) -> int {
    test_support::load_config();
    return test_support::run_tests_with_app(argc, argv);
}
