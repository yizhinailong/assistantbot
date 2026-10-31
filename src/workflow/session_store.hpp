/**
 * @file session_store.hpp
 * @brief Declare the per-user conversation state of private chats.
 */
#pragma once

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace workflow {

    class BotSettings;

    /**
     * @brief One exchange: what the user said and what the model answered.
     */
    struct Turn {
        std::string user;      ///< User message, as sent to the model.
        std::string assistant; ///< Model reply, as sent to the user.
    };

    /**
     * @brief History of one user at the moment it was read.
     */
    struct HistorySnapshot {
        std::vector<Turn> turns;    ///< Recorded turns, oldest first.
        std::uint64_t generation{}; ///< Value that `SessionStore::Record()` checks to drop turns made stale by a reset.
    };

    /**
     * @brief Metadata copied under the session mutex.
     */
    struct SessionSummary {
        std::int64_t user_id{};     ///< Friend QQ number.
        std::string nickname;       ///< Last accepted sender nickname.
        std::size_t turns{};        ///< Number of retained conversation turns.
        std::int64_t last_active{}; ///< Latest activity in UTC Unix seconds.
        bool busy{};                ///< Whether a reply loop owns the session.
    };

    /**
     * @brief One conversation's metadata and immutable turn copy.
     */
    struct SessionSnapshot {
        SessionSummary summary;    ///< Session metadata.
        std::vector<Turn> history; ///< Retained turns in chronological order.
    };

    /**
     * @brief Keep the conversation of each private chat user and serialize the replies to one user.
     *
     * A handler calls `Enqueue()` for every message. Only a call that returns true starts
     * answering: it loops over `TakePending()`, which joins the messages that arrived in the
     * meantime, until nothing is left. So one user never has two model requests at once,
     * and messages sent while a reply is being generated are answered together.
     *
     * Every method locks one mutex for a short time, so handlers on any IO thread may call
     * them, but never hold the lock across `co_await`.
     */
    class SessionStore {
    public:
        using Clock = std::chrono::steady_clock; ///< Clock of the idle timeout.

    private:
        /**
         * @brief State of one user.
         */
        struct Session {
            std::deque<Turn> history;         ///< Recorded turns, oldest first.
            std::vector<std::string> pending; ///< Messages not yet answered, oldest first.
            bool busy{ false };               ///< Whether a handler is answering this user.
            std::uint64_t generation{};       ///< Incremented whenever the history is cleared.
            std::string nickname;             ///< Last accepted sender nickname.
            std::int64_t last_active_utc{};   ///< Latest activity as UTC Unix seconds.
            Clock::time_point last_active{};  ///< Time of the latest message or recorded turn.
        };

        std::shared_ptr<BotSettings const> m_settings;        ///< Optional live policy source.
        std::size_t m_max_turns;                              ///< Most turns kept per user.
        Clock::duration m_idle_reset;                         ///< Silence after which the history is cleared.
        mutable std::mutex m_mutex;                           ///< Guards `m_sessions`.
        std::unordered_map<std::int64_t, Session> m_sessions; ///< Sessions by QQ number.

    public:
        /**
         * @brief Create an empty store.
         * @param max_turns Most turns kept per user; 0 keeps none.
         * @param idle_reset Silence after which a user's history is cleared.
         */
        SessionStore(std::size_t max_turns, Clock::duration idle_reset);

        /**
         * @brief Bind the store to immutable live settings snapshots.
         * @param settings Shared source of history and idle limits.
         */
        explicit SessionStore(std::shared_ptr<BotSettings const> settings);

        /**
         * @brief Record the identity and wall time of an accepted friend message.
         * @param user_id Friend QQ number.
         * @param nickname Current nickname.
         * @param timestamp UTC Unix seconds.
         */
        auto Touch(std::int64_t user_id, std::string nickname, std::int64_t timestamp) -> void;

        /**
         * @brief Copy metadata for every in-memory session under one lock.
         * @return Independent summary records.
         */
        [[nodiscard]] auto Summaries() const -> std::vector<SessionSummary>;

        /**
         * @brief Copy one conversation without creating a missing session.
         * @param user_id Friend QQ number.
         * @return Metadata and turns, or empty when absent.
         */
        [[nodiscard]] auto Detail(std::int64_t user_id) const -> std::optional<SessionSnapshot>;

        /**
         * @brief Queue a user message, and claim the user's replies if nobody is answering.
         *
         * Clears the history first when the user has been silent longer than the idle timeout.
         * @param user_id QQ number of the user.
         * @param text Message text.
         * @param now Current time.
         * @return True if the caller must now answer the user through `TakePending()`;
         * false if another caller already does and will pick the message up.
         */
        [[nodiscard]] auto Enqueue(
            std::int64_t user_id,
            std::string text,
            Clock::time_point now
        ) -> bool;

        /**
         * @brief Take the queued messages of a user, joined by line breaks.
         *
         * When nothing is queued, releases the claim that `Enqueue()` granted.
         * @param user_id QQ number of the user.
         * @return The joined messages, or empty when the caller is done.
         */
        [[nodiscard]] auto TakePending(std::int64_t user_id) -> std::optional<std::string>;

        /**
         * @brief Copy the recorded history of a user.
         * @param user_id QQ number of the user.
         * @return The turns, oldest first, and the current generation.
         */
        [[nodiscard]] auto History(std::int64_t user_id) -> HistorySnapshot;

        /**
         * @brief Count the users whose session state has been created.
         * @return Number of sessions under the store lock.
         */
        [[nodiscard]] auto Count() const -> std::size_t;

        /**
         * @brief Append a turn, dropping the oldest ones beyond the limit.
         *
         * Ignores the turn when the history was cleared after `generation` was read.
         * @param user_id QQ number of the user.
         * @param generation `HistorySnapshot::generation` that the turn's request was built from.
         * @param turn Turn to append.
         * @param now Current time.
         */
        auto Record(
            std::int64_t user_id,
            std::uint64_t generation,
            Turn turn,
            Clock::time_point now
        ) -> void;

        /**
         * @brief Forget a user's history and queued messages.
         *
         * A reply that is being generated is still sent, but not recorded.
         * @param user_id QQ number of the user.
         */
        auto Reset(std::int64_t user_id) -> void;

        /**
         * @brief Release a claim after the answering loop failed, and drop the queued messages.
         * @param user_id QQ number of the user.
         */
        auto Release(std::int64_t user_id) -> void;

    private:
        /**
         * @brief Read current limits without holding the session mutex.
         * @return History count and idle duration from one settings snapshot.
         */
        [[nodiscard]] auto limits() const -> std::pair<std::size_t, Clock::duration>;

        /**
         * @brief Clear a session's history and invalidate the turns being generated.
         * @param session Session to clear; the caller holds `m_mutex`.
         */
        static auto clearHistory(Session& session) -> void;
    };

} // namespace workflow
