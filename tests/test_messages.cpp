/**
 * @file test_messages.cpp
 * @brief Verify message persistence, independent friends, and cursor pagination during appends.
 */
#include <vector>

#include <drogon/drogon_test.h>

#include "storage/database.hpp"
#include "storage/messages.hpp"
#include "support.hpp"

DROGON_TEST(MessagesSurviveReopeningAndCursorPagesRemainStable) {
    auto const directory{ test_support::fresh_directory("messages") };
    std::vector<std::int64_t> ids{};
    constexpr std::int64_t QQ{ 9007199254740993LL };
    {
        auto const db{ storage::open_database(directory) };
        for (int i{}; i < 5; ++i) {
            auto const saved{ drogon::sync_wait(storage::append_message(db, {
                                                                                .timestamp = 100 - i,
                                                                                .user_id = QQ,
                                                                                .nickname = "好友",
                                                                                .direction = "incoming",
                                                                                .content = std::to_string(i),
                                                                                .status = "received",
                                                                            })) };
            ids.push_back(std::stoll(saved["id"].asString()));
        }
        drogon::sync_wait(storage::append_message(db, {
                                                          .timestamp = 101,
                                                          .user_id = 2,
                                                          .nickname = "其他好友",
                                                          .direction = "incoming",
                                                          .content = "other friend",
                                                          .status = "ignored",
                                                          .error = "paused",
                                                      }));
        auto const latest{ drogon::sync_wait(storage::message_page(db, QQ, 2)) };
        REQUIRE(latest["messages"].size() == 2);
        CHECK(latest["messages"][0]["content"] == "3");
        CHECK(latest["messages"][1]["content"] == "4");
        CHECK(latest["messages"][0]["user_id"] == "9007199254740993");
        CHECK(latest["has_more"].asBool());
        auto const failed{ drogon::sync_wait(storage::append_message(db, {
                                                                             .timestamp = 102,
                                                                             .user_id = QQ,
                                                                             .nickname = "新昵称",
                                                                             .direction = "outgoing",
                                                                             .content = "failed delivery",
                                                                             .status = "failed",
                                                                             .error = "upstream offline",
                                                                         })) };
        auto const older{ drogon::sync_wait(storage::message_page(db, QQ, 2, ids[3])) };
        CHECK(older["messages"][0]["content"] == "1");
        CHECK(older["messages"][1]["content"] == "2");
        CHECK(older["has_more"].asBool());
        auto const oldest{ drogon::sync_wait(storage::message_page(db, QQ, 2, ids[1])) };
        REQUIRE(oldest["messages"].size() == 1);
        CHECK(oldest["messages"][0]["content"] == "0");
        CHECK(!oldest["has_more"].asBool());
        auto const catch_up{ drogon::sync_wait(storage::message_page(db, QQ, 2, std::nullopt, ids[4])) };
        REQUIRE(catch_up["messages"].size() == 1);
        CHECK(catch_up["messages"][0] == failed);
        CHECK(!catch_up["has_more"].asBool());
        auto const first{ drogon::sync_wait(storage::message_page(db, QQ, 2, std::nullopt, 0)) };
        CHECK(first["messages"][0]["content"] == "0");
        CHECK(first["messages"][1]["content"] == "1");
        CHECK(first["has_more"].asBool());
    }
    auto const reopened{ storage::open_database(directory) };
    auto const peers{ drogon::sync_wait(storage::message_peers(reopened, QQ)) };
    REQUIRE(peers.size() == 1);
    CHECK(peers[0]["nickname"] == "新昵称");
    CHECK(peers[0]["last_active"].asInt64() == 102);
    auto const page{ drogon::sync_wait(storage::message_page(reopened, QQ, 1)) };
    CHECK(page["messages"][0]["status"] == "failed");
    CHECK(page["messages"][0]["error"] == "upstream offline");
    auto const empty{ drogon::sync_wait(storage::message_page(reopened, 3, 1)) };
    CHECK(empty["messages"].empty());
    CHECK(empty["next_after"].isNull());
}

auto main(int argc, char** argv) -> int {
    return drogon::test::run(argc, argv);
}
