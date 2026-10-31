/**
 * @file test_admin_memory.cpp
 * @brief Verify memory, alias, person, affinity, and revision administration.
 */
#include "admin_support.hpp"
using test_support::request_admin;
using test_support::success;

DROGON_TEST(MemoryRoutesRequireAuthenticationAndValidateDomains) {
    for (auto const* path : { "/api/admin/memories", "/api/admin/memory-documents/people", "/api/admin/memory-documents/aliases/123" }) {
        CHECK(request_admin(path, drogon::Get, {}, "")->statusCode() == drogon::k401Unauthorized);
    }
    CHECK(request_admin("/api/admin/memory-documents/secrets")->statusCode() == drogon::k400BadRequest);
    CHECK(request_admin("/api/admin/memory-documents/people/0")->statusCode() == drogon::k400BadRequest);
    CHECK(request_admin("/api/admin/memories?session=0")->statusCode() == drogon::k400BadRequest);
    CHECK(request_admin("/api/admin/memories", drogon::Post, Json::Value{ Json::objectValue })->statusCode() == drogon::k400BadRequest);
}

DROGON_TEST(PersonProfilesAndAliasesUseOptimisticRevisions) {
    Json::Value body{};
    body["revision"] = "0";
    body["value"]["profile"].append("Likes tea");
    body["value"]["affinity"] = 35;
    auto const saved{ success(request_admin("/api/admin/memory-documents/people/123", drogon::Put, body)) };
    CHECK(saved["value"]["affinity"].asInt() == 35);
    CHECK(saved["value"]["profile"][0] == "Likes tea");
    CHECK(request_admin("/api/admin/memory-documents/people/123", drogon::Put, body)->statusCode() == drogon::k409Conflict);
    body["revision"] = saved["revision"];
    body["value"]["affinity"] = 101;
    CHECK(request_admin("/api/admin/memory-documents/people/123", drogon::Put, body)->statusCode() == drogon::k400BadRequest);
    Json::Value alias{};
    alias["qq"] = "123";
    alias["names"].append("Tea friend");
    body["revision"] = "0";
    body["value"] = Json::Value{};
    body["value"]["aliases"].append(alias);
    CHECK(request_admin("/api/admin/memory-documents/aliases/-456", drogon::Put, body)->statusCode() == drogon::k200OK);
    CHECK(success(request_admin("/api/admin/memory-documents/aliases"))["records"].size() == 1);
}

DROGON_TEST(LongTermMemoriesAreConversationScopedAndRejectStaleChanges) {
    Json::Value body{};
    body["session"] = "-456";
    body["content"] = "Likes tea";
    auto const saved{ success(request_admin("/api/admin/memories", drogon::Post, body)) };
    CHECK(saved["dimensions"].asUInt() == 0);
    CHECK(!saved.isMember("embedding"));
    CHECK(success(request_admin("/api/admin/memories?session=123"))["memories"].empty());
    CHECK(success(request_admin("/api/admin/memories?session=-456"))["memories"].size() == 1);
    Json::Value query{};
    query["session"] = "-456";
    query["query"] = "tea";
    CHECK(success(request_admin("/api/admin/memories/search", drogon::Post, query))["memories"].size() == 1);
    auto const path{ "/api/admin/memories/" + saved["id"].asString() };
    body["revision"] = "1";
    body["content"] = "Likes green tea";
    CHECK(success(request_admin(path, drogon::Put, body))["revision"] == "2");
    CHECK(request_admin(path, drogon::Put, body)->statusCode() == drogon::k409Conflict);
    CHECK(request_admin(path + "?revision=1", drogon::Delete)->statusCode() == drogon::k409Conflict);
    CHECK(request_admin(path + "?revision=2", drogon::Delete)->statusCode() == drogon::k200OK);
}

auto main(int argc, char** argv) -> int {
    test_support::load_config();
    return test_support::run_tests_with_app(argc, argv);
}
