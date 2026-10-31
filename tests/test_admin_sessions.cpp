/**
 * @file test_admin_sessions.cpp
 * @brief Verify persistent session discovery, reset behavior, and message query validation.
 */
#include <drogon/drogon_test.h>

#include "admin_support.hpp"
#include "controllers/onebot/assistant_bot.hpp"
#include "storage/database.hpp"
#include "storage/messages.hpp"
#include "workflow/private_chat.hpp"

DROGON_TEST(SessionRoutesRequireAuthenticationAndValidateIdentifiersAndCursors) {
    for (auto const* path : { "/api/admin/sessions", "/api/admin/sessions/42", "/api/admin/sessions/42/messages" }) {
        CHECK(test_support::request_admin(path, drogon::Get, {}, "")->statusCode() == drogon::k401Unauthorized);
    }
    CHECK(test_support::request_admin("/api/admin/sessions/42/reset", drogon::Post, {}, "")->statusCode() == drogon::k401Unauthorized);
    for (auto const* qq : { "0", "-1", "123x", "9223372036854775808" }) {
        CHECK(test_support::request_admin(std::string{ "/api/admin/sessions/" } + qq)->statusCode() == drogon::k400BadRequest);
    }
    for (auto const* query : { "limit=0", "limit=201", "limit=", "before=0", "after=-1", "before=3&after=0", "after=9223372036854775808" }) {
        CHECK(test_support::request_admin(std::string{ "/api/admin/sessions/42/messages?" } + query)->statusCode() == drogon::k400BadRequest);
    }
    auto const chat{ drogon::app().getPlugin<controllers::AssistantBot>()->Chat() };
    auto const before{ chat->SessionCount() };
    CHECK(test_support::request_admin("/api/admin/sessions/999999")->statusCode() == drogon::k404NotFound);
    CHECK(test_support::request_admin("/api/admin/sessions/999999/reset", drogon::Post)->statusCode() == drogon::k404NotFound);
    CHECK(test_support::success(test_support::request_admin("/api/admin/sessions/999999/messages"))["messages"].empty());
    CHECK(chat->SessionCount() == before);
}

DROGON_TEST(SessionListIncludesStoredPeersAndResetPreservesChatRecords) {
    auto const db{ drogon::app().getPlugin<storage::Database>()->Client() };
    auto const saved{ drogon::sync_wait(storage::append_message(db, {
                                                                        .timestamp = 100,
                                                                        .user_id = 9007199254740993LL,
                                                                        .nickname = "重启前好友",
                                                                        .direction = "incoming",
                                                                        .content = "old record",
                                                                        .status = "received",
                                                                    })) };
    auto const sessions{ test_support::success(test_support::request_admin("/api/admin/sessions"))["sessions"] };
    REQUIRE(sessions.size() == 1);
    CHECK(sessions[0]["user_id"] == "9007199254740993");
    CHECK(sessions[0]["nickname"] == "重启前好友");
    CHECK(sessions[0]["turns"].asInt() == 0);
    CHECK(sessions[0]["last_active"].asInt64() == 100);
    CHECK(!sessions[0]["busy"].asBool());
    auto const detail{ test_support::success(test_support::request_admin("/api/admin/sessions/9007199254740993")) };
    CHECK(detail["history"].empty());
    auto const reset{ test_support::success(test_support::request_admin("/api/admin/sessions/9007199254740993/reset", drogon::Post)) };
    CHECK(reset["history"].empty());
    CHECK(reset["nickname"] == "重启前好友");
    CHECK(test_support::success(test_support::request_admin("/api/admin/sessions/9007199254740993/messages?limit=1"))["messages"][0] == saved);

    auto const chat{ drogon::app().getPlugin<controllers::AssistantBot>()->Chat() };
    drogon::sync_wait(chat->Observe({ .self_id = 1, .user_id = 77, .nickname = "在线好友" }, "observed", {}));
    auto const live{ test_support::success(test_support::request_admin("/api/admin/sessions/77")) };
    CHECK(live["nickname"] == "在线好友");
    CHECK(live["last_active"].asInt64() > 100);
    CHECK(live["turns"].asInt() == 0);
}

DROGON_TEST(SessionStorageFailuresReturnJsonErrors) {
    auto const db{ drogon::app().getPlugin<storage::Database>()->Client() };
    db->execSqlSync("ALTER TABLE chat_messages RENAME TO unavailable_messages");
    for (auto const* path : { "/api/admin/sessions", "/api/admin/sessions/42", "/api/admin/sessions/42/messages" }) {
        auto const response{ test_support::request_admin(path) };
        CHECK(response->statusCode() == drogon::k500InternalServerError);
        REQUIRE(response->getJsonObject() != nullptr);
        CHECK((*response->getJsonObject())["error"].isString());
    }
    db->execSqlSync("ALTER TABLE unavailable_messages RENAME TO chat_messages");
}

auto main(int argc, char** argv) -> int {
    test_support::load_config();
    return test_support::run_tests_with_app(argc, argv);
}
