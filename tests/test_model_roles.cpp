/**
 * @file test_model_roles.cpp
 * @brief Verify role isolation, credential inheritance, embeddings, images, and usage accounting.
 */
#include "admin/http_traces.hpp"
#include "admin_support.hpp"
#include "inbox.hpp"
#include "storage/database.hpp"
#include "storage/usage.hpp"
#include "workflow/feature_settings.hpp"
#include "workflow/model_roles.hpp"

namespace {
    test_support::Inbox<Json::Value> calls{}; ///< Requests observed by the fake role endpoint.
}

DROGON_TEST(CustomRoleEndpointsDoNotInheritPrimaryCredentials) {
    auto const db{ drogon::app().getPlugin<storage::Database>()->Client() };
    workflow::ModelRoles models{ drogon_openai::OpenAI{ { .base_url = std::string{ test_support::SERVER_URL } + "/role", .api_key = "primary-secret", .model = "primary" } }, db, "primary" };
    auto snapshot{ workflow::default_feature_settings() };
    auto& role{ snapshot["models"]["thinking"] };
    role["enabled"] = true;
    role["base_url"] = std::string{ test_support::SERVER_URL } + "/role";
    role["model"] = "think-model";
    role["max_tokens"] = 31;
    auto result{ drogon::sync_wait(models.Complete(snapshot, "thinking", 123, { .messages = { { .role = "user", .content = "test" } } })) };
    REQUIRE(result.has_value());
    auto request{ calls.Pop() };
    REQUIRE(request.has_value());
    CHECK((*request)["authorization"] == "");
    CHECK((*request)["body"]["model"] == "think-model");
    CHECK((*request)["body"]["max_tokens"].asInt() == 31);
    role["api_key_env"] = "ASSISTANTBOT_TEST_ROLE_KEY";
    result = drogon::sync_wait(models.Complete(snapshot, "thinking", 123, { .messages = { { .role = "user", .content = "test" } } }));
    REQUIRE(result.has_value());
    request = calls.Pop();
    REQUIRE(request.has_value());
    CHECK((*request)["authorization"] == "Bearer role-secret");
    CHECK(!admin::read_http_traces()["traces"].empty());
    CHECK(!admin::read_http_traces().toStyledString().contains("role-secret"));
    CHECK_THROWS_AS(static_cast<void>(drogon::sync_wait(models.Complete(snapshot, "decision", 123, {}))), std::invalid_argument);
}

DROGON_TEST(EmbeddingAndImageRolesUseTypedResourcesAndRecordUsage) {
    auto const db{ drogon::app().getPlugin<storage::Database>()->Client() };
    workflow::ModelRoles models{ drogon_openai::OpenAI{ { .base_url = std::string{ test_support::SERVER_URL } + "/role", .model = "primary" } }, db, "primary" };
    auto snapshot{ workflow::default_feature_settings() };
    for (auto const* name : { "embedding", "image_generation" }) {
        snapshot["models"][name]["enabled"] = true;
        snapshot["models"][name]["model"] = name;
    }
    auto const vector{ drogon::sync_wait(models.Embed(snapshot, 124, "remember this")) };
    CHECK(vector == std::vector<double>({ 1.0, 0.5, 0.0 }));
    CHECK(models.EmbeddingSpace(snapshot).ends_with("|embedding"));
    auto const image{ drogon::sync_wait(models.Image(snapshot, 124, "a cat")) };
    CHECK(image == "base64://aW1hZ2U=");
    auto const usage{ drogon::sync_wait(storage::summarize_usage(db, 0, INT64_MAX)) };
    CHECK(usage["requests"].asInt64() >= 2);
}

auto main(int argc, char** argv) -> int {
    test_support::load_config();
    test_support::set_env("ASSISTANTBOT_TEST_ROLE_KEY", "role-secret");
    for (auto const* path : { "/role/chat/completions", "/role/embeddings", "/role/images/generations" }) {
        drogon::app().registerHandler(path, [path](drogon::HttpRequestPtr req) -> drogon::Task<drogon::HttpResponsePtr> {
            Json::Value observed{};
            observed["authorization"] = req->getHeader("authorization");
            observed["body"] = *req->getJsonObject();
            if (std::string_view{ path }.ends_with("completions")) {
                calls.Push(observed);
            }
            Json::Value body{};
            if (std::string_view{ path }.ends_with("completions")) {
                body["id"] = "role-test";
                body["object"] = "chat.completion";
                body["created"] = 1;
                body["model"] = (*req->getJsonObject())["model"];
                Json::Value choice{};
                choice["index"] = 0;
                choice["message"]["role"] = "assistant";
                choice["message"]["content"] = "hello";
                choice["finish_reason"] = "stop";
                body["choices"].append(choice);
            } else if (std::string_view{ path }.ends_with("embeddings")) {
                body["model"] = "embedding";
                body["object"] = "list";
                Json::Value embedding{};
                embedding["object"] = "embedding";
                embedding["index"] = 0;
                embedding["embedding"].append(1.0);
                embedding["embedding"].append(0.5);
                embedding["embedding"].append(0.0);
                body["data"].append(embedding);
            } else {
                body["created"] = 1;
                Json::Value image{};
                image["b64_json"] = "aW1hZ2U=";
                body["data"].append(image);
            }
            body["usage"]["prompt_tokens"] = 2;
            body["usage"]["completion_tokens"] = 1;
            body["usage"]["total_tokens"] = 3;
            co_return drogon::HttpResponse::newHttpJsonResponse(body);
        },
                                      { drogon::Post });
    }
    return test_support::run_tests_with_app(argc, argv);
}
