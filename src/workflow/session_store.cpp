/**
 * @file session_store.cpp
 * @brief Implement the per-user conversation state of private chats.
 */
#include "workflow/session_store.hpp"

#include <algorithm>
#include <span>
#include <utility>

#include "common/time.hpp"
#include "workflow/bot_settings.hpp"

namespace workflow {

    /**
     * @brief Create an empty store.
     * @param max_turns Most turns kept per user; 0 keeps none.
     * @param idle_reset Silence after which a user's history is cleared.
     */
    SessionStore::SessionStore(std::size_t max_turns, Clock::duration idle_reset)
        : m_max_turns{ max_turns },
          m_idle_reset{ idle_reset } {}

    /**
     * @brief Bind the store to immutable live settings snapshots.
     * @param settings Shared source of history and idle limits.
     */
    SessionStore::SessionStore(std::shared_ptr<BotSettings const> settings)
        : SessionStore{ 10, std::chrono::minutes{ 30 } } {
        m_settings = std::move(settings);
    }

    /**
     * @brief Read current limits without holding the session mutex.
     * @return History count and idle duration from one settings snapshot.
     */
    auto SessionStore::limits() const -> std::pair<std::size_t, Clock::duration> {
        if (m_settings) {
            auto const current{ m_settings->Current() };
            return { current->history_turns, current->idle_reset };
        }
        return { m_max_turns, m_idle_reset };
    }

    /**
     * @brief Record the identity and wall time of an accepted friend message.
     * @param user_id Friend QQ number.
     * @param nickname Current nickname.
     * @param timestamp UTC Unix seconds.
     */
    auto SessionStore::Touch(
        std::int64_t user_id,
        std::string nickname,
        std::int64_t timestamp
    ) -> void {
        std::lock_guard lock{ m_mutex };
        auto& session{ m_sessions[user_id] };
        session.nickname = std::move(nickname);
        session.last_active_utc = timestamp;
    }

    /**
     * @brief Copy metadata for every in-memory session under one lock.
     * @return Independent summary records.
     */
    auto SessionStore::Summaries() const -> std::vector<SessionSummary> {
        auto const max_turns{ limits().first };
        std::lock_guard lock{ m_mutex };
        std::vector<SessionSummary> result{};
        result.reserve(m_sessions.size());
        for (auto const& [user_id, session] : m_sessions) {
            result.push_back({ .user_id = user_id, .nickname = session.nickname, .turns = std::min(session.history.size(), max_turns), .last_active = session.last_active_utc, .busy = session.busy });
        }
        return result;
    }

    /**
     * @brief Copy one conversation without creating a missing session.
     * @param user_id Friend QQ number.
     * @return Metadata and turns, or empty when absent.
     */
    auto SessionStore::Detail(std::int64_t user_id) const -> std::optional<SessionSnapshot> {
        auto const max_turns{ limits().first };
        std::lock_guard lock{ m_mutex };
        auto const found{ m_sessions.find(user_id) };
        if (found == m_sessions.end()) {
            return std::nullopt;
        }
        auto const& session{ found->second };
        auto const count{ std::min(session.history.size(), max_turns) };
        return SessionSnapshot{
            .summary = { .user_id = user_id, .nickname = session.nickname, .turns = count, .last_active = session.last_active_utc, .busy = session.busy },
            .history = { session.history.end() - static_cast<std::ptrdiff_t>(count), session.history.end() },
        };
    }

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
    auto SessionStore::Enqueue(
        std::int64_t user_id,
        std::string text,
        Clock::time_point now
    ) -> bool {
        auto const [max_turns, idle_reset]{ limits() };
        std::lock_guard lock{ m_mutex };
        auto& session{ m_sessions[user_id] };
        while (session.history.size() > max_turns) {
            session.history.pop_front();
        }
        if (now - session.last_active > idle_reset) {
            clearHistory(session);
        }
        session.last_active = now;
        session.last_active_utc = common::unix_seconds();
        session.pending.push_back(std::move(text));
        if (session.busy) {
            return false;
        }
        session.busy = true;
        return true;
    }

    /**
     * @brief Take the queued messages of a user, joined by line breaks.
     *
     * When nothing is queued, releases the claim that `Enqueue()` granted.
     * @param user_id QQ number of the user.
     * @return The joined messages, or empty when the caller is done.
     */
    auto SessionStore::TakePending(std::int64_t user_id) -> std::optional<std::string> {
        std::lock_guard lock{ m_mutex };
        auto& session{ m_sessions[user_id] };
        if (session.pending.empty()) {
            session.busy = false;
            return std::nullopt;
        }
        // Not `| std::ranges::to<std::string>()`: clang rejects it with libstdc++ 14, which CI uses.
        auto joined{ std::move(session.pending.front()) };
        for (auto const& text : std::span{ session.pending }.subspan(1)) {
            joined += '\n';
            joined += text;
        }
        session.pending.clear();
        return joined;
    }

    /**
     * @brief Copy the recorded history of a user.
     * @param user_id QQ number of the user.
     * @return The turns, oldest first, and the current generation.
     */
    auto SessionStore::History(std::int64_t user_id) -> HistorySnapshot {
        auto const max_turns{ limits().first };
        std::lock_guard lock{ m_mutex };
        auto& session{ m_sessions[user_id] };
        while (session.history.size() > max_turns) {
            session.history.pop_front();
        }
        return { .turns = { session.history.begin(), session.history.end() }, .generation = session.generation };
    }

    /**
     * @brief Count the users whose session state has been created.
     * @return Number of sessions under the store lock.
     */
    auto SessionStore::Count() const -> std::size_t {
        std::lock_guard lock{ m_mutex };
        return m_sessions.size();
    }

    /**
     * @brief Append a turn, dropping the oldest ones beyond the limit.
     *
     * Ignores the turn when the history was cleared after `generation` was read.
     * @param user_id QQ number of the user.
     * @param generation `HistorySnapshot::generation` that the turn's request was built from.
     * @param turn Turn to append.
     * @param now Current time.
     */
    auto SessionStore::Record(
        std::int64_t user_id,
        std::uint64_t generation,
        Turn turn,
        Clock::time_point now
    ) -> void {
        auto const max_turns{ limits().first };
        std::lock_guard lock{ m_mutex };
        auto& session{ m_sessions[user_id] };
        if (generation != session.generation) {
            return;
        }
        session.history.push_back(std::move(turn));
        while (session.history.size() > max_turns) {
            session.history.pop_front();
        }
        session.last_active = now;
        session.last_active_utc = common::unix_seconds();
    }

    /**
     * @brief Forget a user's history and queued messages.
     *
     * A reply that is being generated is still sent, but not recorded.
     * @param user_id QQ number of the user.
     */
    auto SessionStore::Reset(std::int64_t user_id) -> void {
        std::lock_guard lock{ m_mutex };
        auto& session{ m_sessions[user_id] };
        clearHistory(session);
        session.last_active_utc = common::unix_seconds();
        session.pending.clear();
    }

    /**
     * @brief Release a claim after the answering loop failed, and drop the queued messages.
     * @param user_id QQ number of the user.
     */
    auto SessionStore::Release(std::int64_t user_id) -> void {
        std::lock_guard lock{ m_mutex };
        auto& session{ m_sessions[user_id] };
        session.busy = false;
        session.pending.clear();
    }

    /**
     * @brief Clear a session's history and invalidate the turns being generated.
     * @param session Session to clear; the caller holds `m_mutex`.
     */
    auto SessionStore::clearHistory(Session& session) -> void {
        session.history.clear();
        ++session.generation;
    }

} // namespace workflow
