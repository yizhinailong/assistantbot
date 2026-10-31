/**
 * @file test_health.cpp
 * @brief Check the administrative health endpoint on the running app.
 */
#include <drogon/drogon.h>
#include <drogon/drogon_test.h>

#include "support.hpp"

DROGON_TEST(HealthReportsOk) {
    auto request{ drogon::HttpRequest::newHttpRequest() };
    request->setMethod(drogon::Get);
    request->setPath("/api/admin/health");
    auto const client{ drogon::HttpClient::newHttpClient(test_support::SERVER_URL) };
    auto const [result, response]{ client->sendRequest(request, 5.0) };
    REQUIRE(result == drogon::ReqResult::Ok);
    CHECK(response->statusCode() == drogon::k200OK);

    auto const body{ response->getJsonObject() };
    REQUIRE(body != nullptr);
    CHECK((*body)["status"].asString() == "ok");
}

auto main(int argc, char** argv) -> int {
    test_support::load_config();
    return test_support::run_tests_with_app(argc, argv);
}
