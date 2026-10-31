/**
 * @file scheduled_tasks.hpp
 * @brief Persist conversation reminders and execute them after restart without duplicate claims.
 */
#pragma once
#include <atomic>
#include <functional>
#include <memory>
#include <mutex>

#include <trantor/net/EventLoop.h>

#include "workflow/private_chat.hpp"

namespace workflow {
    /**
     * @brief Own durable UTC tasks and a lightweight nearest-deadline timer.
     */
    class ScheduledTasks : public std::enable_shared_from_this<ScheduledTasks> {
    public:
        using Handler = std::function<drogon::Task<>(Peer, std::string)>; ///< Delivery callback, registered before listeners start.

    private:
        drogon::orm::DbClientPtr m_database; ///< Task database.
        Handler m_handler;                   ///< Shared bot delivery path.
        std::mutex m_mutex;                  ///< Protects nearest deadline and wake revision.
        std::int64_t m_nextDue{ INT64_MAX }; ///< No database polling while no pending task exists.
        std::uint64_t m_revision{};          ///< Detects changes during a poll.
        std::atomic_bool m_running{};        ///< One due-task batch at a time.
        std::atomic_bool m_stopped{};        ///< Suppresses new delivery at shutdown.
        trantor::TimerId m_timer{};          ///< App-loop periodic timer.

    public:
        /**
         * @brief Recover pending deadlines and mark interrupted in-flight work as failed.
         * @param database Database client; construction runs before event loops start.
         * @param handler Callback that rechecks pause and recipient access before delivery.
         */
        ScheduledTasks(drogon::orm::DbClientPtr database, Handler handler);
        /**
         * @brief Register the timer through a beginning advice after shared ownership exists.
         */
        auto Start() -> void;
        /**
         * @brief Cancel timer polling and suppress new task claims.
         */
        auto Stop() -> void;
        /**
         * @brief Observe whether an already-started poll still needs its event loop.
         * @return True until the current delivery batch and storage continuations finish.
         */
        [[nodiscard]] auto Busy() const noexcept -> bool;
        /**
         * @brief Create one durable reminder for a private or group conversation.
         * @param peer Recipient and actual creator identity.
         * @param due_at UTC Unix seconds, within the next 366 days.
         * @param content Prompt delivered to the conversation when due.
         * @param daily Whether to repeat at the same UTC time every day.
         * @return Persisted task.
         */
        auto Create(
            Peer peer,
            std::int64_t due_at,
            std::string content,
            bool daily
        ) -> drogon::Task<Json::Value>;
        /**
         * @brief List bounded persistent tasks, optionally restricted to one conversation.
         * @param session Optional conversation key.
         * @return Up to 500 task records, newest first.
         */
        auto List(std::optional<std::int64_t> session = {}) -> drogon::Task<Json::Value>;
        /**
         * @brief Cancel a pending task only if its optional conversation restriction matches.
         * @param id Task identifier.
         * @param session Optional key restricting model tools to the current conversation.
         * @return Whether a pending task was cancelled.
         */
        auto Cancel(
            std::int64_t id,
            std::optional<std::int64_t> session = {}
        ) -> drogon::Task<bool>;

    private:
        /**
         * @brief Wake the nearest-deadline timer after a persistent state change.
         */
        auto wake() -> void;
        /**
         * @brief Claim and execute bounded due work, then cache the next deadline.
         * @return Task completing after the due batch; failures are recorded or logged.
         */
        auto tick() -> drogon::Task<>;
    };
} // namespace workflow
