/**
 * @file test_admin_features.cpp
 * @brief Check authenticated feature sections, opt-in groups, validation, and conflicts.
 */
#include "admin_support.hpp"

using test_support::request_admin;
using test_support::success;

DROGON_TEST(FeatureEndpointsRequireAuthentication) {
    CHECK(request_admin("/api/admin/features/models", drogon::Get, {}, "")->statusCode() == drogon::k401Unauthorized);
    CHECK(request_admin("/api/admin/groups", drogon::Get, {}, "")->statusCode() == drogon::k401Unauthorized);
    CHECK(request_admin("/api/admin/groups/123", drogon::Put, {}, "")->statusCode() == drogon::k401Unauthorized);
}

DROGON_TEST(GroupMembershipDefaultsOffAndUpdatesConflictRatherThanOverwrite) {
    auto const list{ success(request_admin("/api/admin/groups")) };
    REQUIRE(list["groups"].size() == 1);
    CHECK(list["groups"][0]["group_id"] == "123");
    CHECK(!list["groups"][0]["values"]["enabled"].asBool());
    Json::Value body{};
    body["values"] = list["groups"][0]["values"];
    body["values"]["enabled"] = true;
    body["values"]["additional_prompt"] = "Group prompt";
    body["revision"] = list["revision"];
    auto const saved{ success(request_admin("/api/admin/groups/123", drogon::Put, body)) };
    CHECK(saved["values"]["123"]["enabled"].asBool());
    CHECK(request_admin("/api/admin/groups/123", drogon::Put, body)->statusCode() == drogon::k409Conflict);
    body["revision"] = saved["revision"];
    body["values"]["enabled"] = "yes";
    CHECK(request_admin("/api/admin/groups/123", drogon::Put, body)->statusCode() == drogon::k400BadRequest);
    CHECK(request_admin("/api/admin/groups/0", drogon::Put, body)->statusCode() == drogon::k400BadRequest);
}

DROGON_TEST(FeatureApiRejectsSecretsAndReturnsValidatedDefaults) {
    auto const config{ success(request_admin("/api/admin/features/models")) };
    Json::Value body{};
    body["values"]["vision"]["api_key"] = "never-store-this";
    body["revision"] = config["revision"];
    CHECK(request_admin("/api/admin/features/models", drogon::Put, body)->statusCode() == drogon::k400BadRequest);
    body["values"] = Json::Value{ Json::objectValue };
    body["values"]["vision"]["api_key_env"] = "VISION_KEY";
    auto const updated{ success(request_admin("/api/admin/features/models", drogon::Put, body)) };
    CHECK(updated["values"]["vision"]["api_key_env"] == "VISION_KEY");
    CHECK(!updated["values"]["vision"].isMember("api_key"));
    CHECK(request_admin("/api/admin/features/unknown")->statusCode() == drogon::k400BadRequest);
}

auto main(int argc, char** argv) -> int {
    test_support::load_config();
    drogon::app().registerHandler("/napcat/get_group_list", [](drogon::HttpRequestPtr) -> drogon::Task<drogon::HttpResponsePtr> {
        Json::Value body{};
        body["status"] = "ok";
        body["retcode"] = 0;
        Json::Value group{};
        group["group_id"] = "123";
        group["group_name"] = "Test group";
        group["member_count"] = 4;
        body["data"].append(group);
        co_return drogon::HttpResponse::newHttpJsonResponse(body);
    },
                                  { drogon::Post });
    return test_support::run_tests_with_app(argc, argv);
}
