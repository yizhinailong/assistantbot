/**
 * @file test_session_store.cpp
 * @brief Check how `SessionStore` queues messages, records turns, and clears history.
 */
#include <chrono>
#include <cstdint>
#include <string>

#include <drogon/drogon_test.h>

#include "storage/database.hpp"
#include "support.hpp"
#include "workflow/bot_settings.hpp"
#include "workflow/session_store.hpp"

namespace {

    using Clock = workflow::SessionStore::Clock;

    constexpr std::int64_t ALICE{ 20001 };                   ///< QQ number of one user.
    constexpr std::int64_t BOB{ 20002 };                     ///< QQ number of another user.
    constexpr auto IDLE_RESET{ std::chrono::minutes{ 30 } }; ///< Idle timeout of the stores under test.

    /**
     * @brief Build a turn whose reply echoes the message.
     * @param text User message.
     * @return Turn with `text` and `echo: <text>`.
     */
    auto echo_turn(std::string const& text) -> workflow::Turn {
        return { .user = text, .assistant = "echo: " + text };
    }

    /**
     * @brief List the user messages of a history.
     * @param snapshot History to read.
     * @return The `Turn::user` values, oldest first, joined by commas.
     */
    auto user_messages(workflow::HistorySnapshot const& snapshot) -> std::string {
        std::string result{};
        for (auto const& turn : snapshot.turns) {
            result += result.empty() ? turn.user : "," + turn.user;
        }
        return result;
    }

} // namespace

DROGON_TEST(EnqueueClaimsTheUserOnce) {
    workflow::SessionStore store{ 10, IDLE_RESET };
    auto const now{ Clock::now() };
    CHECK(store.Enqueue(ALICE, "a", now));
    CHECK(store.TakePending(ALICE) == "a");

    // Messages that arrive while the reply is being generated wait for the claimer.
    CHECK(!store.Enqueue(ALICE, "b", now));
    CHECK(!store.Enqueue(ALICE, "c", now));
    CHECK(store.TakePending(ALICE) == "b\nc");

    // An empty queue releases the claim, so the next message claims again.
    CHECK(!store.TakePending(ALICE).has_value());
    CHECK(store.Enqueue(ALICE, "d", now));
}

DROGON_TEST(UsersAreIndependent) {
    workflow::SessionStore store{ 10, IDLE_RESET };
    auto const now{ Clock::now() };
    CHECK(store.Enqueue(ALICE, "a", now));
    CHECK(store.Enqueue(BOB, "b", now));
    store.Record(ALICE, store.History(ALICE).generation, echo_turn("a"), now);
    CHECK(user_messages(store.History(ALICE)) == "a");
    CHECK(store.History(BOB).turns.empty());
}

DROGON_TEST(RecordKeepsTheLatestTurns) {
    workflow::SessionStore store{ 2, IDLE_RESET };
    auto const now{ Clock::now() };
    for (auto const* text : { "one", "two", "three" }) {
        store.Record(ALICE, store.History(ALICE).generation, echo_turn(text), now);
    }
    auto const history{ store.History(ALICE) };
    CHECK(user_messages(history) == "two,three");
    CHECK(history.turns.back().assistant == "echo: three");
}

DROGON_TEST(ZeroTurnsKeepsNoHistory) {
    workflow::SessionStore store{ 0, IDLE_RESET };
    store.Record(ALICE, store.History(ALICE).generation, echo_turn("one"), Clock::now());
    CHECK(store.History(ALICE).turns.empty());
}

DROGON_TEST(SilenceClearsTheHistory) {
    workflow::SessionStore store{ 10, IDLE_RESET };
    auto const start{ Clock::now() };
    CHECK(store.Enqueue(ALICE, "one", start));
    CHECK(store.TakePending(ALICE) == "one");
    store.Record(ALICE, store.History(ALICE).generation, echo_turn("one"), start);
    CHECK(!store.TakePending(ALICE).has_value());

    // Within the timeout the history stays.
    CHECK(store.Enqueue(ALICE, "two", start + IDLE_RESET));
    CHECK(user_messages(store.History(ALICE)) == "one");
    CHECK(store.TakePending(ALICE) == "two");
    CHECK(!store.TakePending(ALICE).has_value());

    // Measured from the latest message, a longer silence clears it.
    CHECK(store.Enqueue(ALICE, "three", start + 2 * IDLE_RESET + std::chrono::seconds{ 1 }));
    CHECK(store.History(ALICE).turns.empty());
}

DROGON_TEST(ResetDropsHistoryQueueAndStaleTurns) {
    workflow::SessionStore store{ 10, IDLE_RESET };
    auto const now{ Clock::now() };
    store.Record(ALICE, store.History(ALICE).generation, echo_turn("old"), now);
    CHECK(store.Enqueue(ALICE, "a", now));
    CHECK(store.TakePending(ALICE) == "a");
    auto const before_reset{ store.History(ALICE) };
    CHECK(!store.Enqueue(ALICE, "b", now));

    store.Reset(ALICE);
    CHECK(store.History(ALICE).turns.empty());

    // The reply that was in flight during the reset is not recorded.
    store.Record(ALICE, before_reset.generation, echo_turn("a"), now);
    CHECK(store.History(ALICE).turns.empty());
    CHECK(!store.TakePending(ALICE).has_value());
}

DROGON_TEST(ReleaseFreesTheClaim) {
    workflow::SessionStore store{ 10, IDLE_RESET };
    auto const now{ Clock::now() };
    CHECK(store.Enqueue(ALICE, "a", now));
    CHECK(!store.Enqueue(ALICE, "b", now));
    store.Release(ALICE);
    CHECK(store.Enqueue(ALICE, "c", now));
    CHECK(store.TakePending(ALICE) == "c");
}

DROGON_TEST(SnapshotsCopyIdentityContextAndBusyStateWithoutCreatingSessions) {
    workflow::SessionStore store{ 2, IDLE_RESET };
    CHECK(!store.Detail(ALICE));
    CHECK(store.Count() == 0);
    store.Touch(ALICE, "Alice", 123);
    store.Record(ALICE, store.History(ALICE).generation, echo_turn("old"), Clock::now());
    CHECK(store.Enqueue(ALICE, "pending", Clock::now()));
    auto const snapshot{ store.Detail(ALICE) };
    REQUIRE(snapshot.has_value());
    CHECK(snapshot->summary.nickname == "Alice");
    CHECK(snapshot->summary.busy);
    CHECK(snapshot->summary.turns == 1);
    CHECK(snapshot->summary.last_active > 0);
    auto const summaries{ store.Summaries() };
    REQUIRE(summaries.size() == 1);
    CHECK(summaries[0].user_id == ALICE);
    store.Reset(ALICE);
    store.Touch(ALICE, "renamed", 456);
    CHECK(snapshot->history[0].user == "old");
    CHECK(snapshot->summary.nickname == "Alice");
    CHECK(store.Detail(ALICE)->history.empty());
    CHECK(store.Detail(ALICE)->summary.busy);
    CHECK(!store.TakePending(ALICE));
    CHECK(!store.Detail(ALICE)->summary.busy);
}

DROGON_TEST(SessionLimitsFollowSavedSettingsWithoutRestart) {
    auto const db{ storage::open_database(test_support::fresh_directory("session-settings")) };
    Json::Value defaults{ Json::objectValue };
    defaults["history_turns"] = 3;
    defaults["idle_reset_minutes"] = 30;
    auto const settings{ std::make_shared<workflow::BotSettings>(defaults, db) };
    workflow::SessionStore store{ settings };
    auto const start{ Clock::now() };
    for (auto const* text : { "one", "two", "three" }) {
        store.Record(ALICE, store.History(ALICE).generation, echo_turn(text), start);
    }
    Json::Value overrides{ Json::objectValue };
    overrides["history_turns"] = 1;
    overrides["idle_reset_minutes"] = 1;
    drogon::sync_wait(settings->Update(overrides));
    CHECK(store.Detail(ALICE)->summary.turns == 1);
    CHECK(user_messages(store.History(ALICE)) == "three");
    CHECK(store.Enqueue(ALICE, "after silence", start + std::chrono::seconds{ 61 }));
    CHECK(store.History(ALICE).turns.empty());
    overrides["history_turns"] = 0;
    drogon::sync_wait(settings->Update(overrides));
    store.Record(ALICE, store.History(ALICE).generation, echo_turn("discarded"), start);
    CHECK(store.History(ALICE).turns.empty());
    drogon::sync_wait(settings->Update(Json::Value{ Json::objectValue }));
    for (auto const* text : { "a", "b", "c" }) {
        store.Record(ALICE, store.History(ALICE).generation, echo_turn(text), start);
    }
    CHECK(user_messages(store.History(ALICE)) == "a,b,c");
}

auto main(int argc, char** argv) -> int {
    return drogon::test::run(argc, argv);
}
