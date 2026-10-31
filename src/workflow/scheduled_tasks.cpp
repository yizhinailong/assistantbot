/**
 * @file scheduled_tasks.cpp
 * @brief Claim UTC reminders atomically and avoid replaying interrupted model deliveries.
 */
#include "workflow/scheduled_tasks.hpp"

#include <drogon/drogon.h>

#include "common/time.hpp"
#include "storage/query.hpp"

namespace {
    /**
     * @brief Convert one task row to an administrative record.
     * @param row SQLite task row.
     * @return Precision-preserving task document.
     */
    auto task_json(drogon::orm::Row const& row) -> Json::Value {
        Json::Value task{};
        for (auto const* key : { "id", "session_id", "sender_id" }) {
            task[key] = std::to_string(row[key].as<std::int64_t>());
        }
        for (auto const* key : { "due_at", "created_at" }) {
            task[key] = Json::Int64{ row[key].as<std::int64_t>() };
        }
        for (auto const* key : { "content", "status", "error" }) {
            task[key] = row[key].as<std::string>();
        }
        task["daily"] = row["daily"].as<int>() != 0;
        return task;
    }
} // namespace

namespace workflow {
    /**
     * @brief Recover pending deadlines and mark interrupted in-flight work as failed.
     * @param database Database client; construction runs before event loops start.
     * @param handler Callback that rechecks pause and recipient access before delivery.
     */
    ScheduledTasks::ScheduledTasks(drogon::orm::DbClientPtr database, Handler handler)
        : m_database{ std::move(database) }, m_handler{ std::move(handler) } {
        m_database->execSqlSync("UPDATE scheduled_tasks SET status = 'failed', error = 'interrupted by restart; delivery was not replayed' WHERE status = 'running'");
        auto const rows{ m_database->execSqlSync("SELECT min(due_at) AS next FROM scheduled_tasks WHERE status = 'pending'") };
        if (!rows[0]["next"].isNull()) {
            m_nextDue = rows[0]["next"].as<std::int64_t>();
        }
    }

    /**
     * @brief Register the timer through a beginning advice after shared ownership exists.
     */
    auto ScheduledTasks::Start() -> void {
        std::weak_ptr<ScheduledTasks> weak{ shared_from_this() };
        drogon::app().registerBeginningAdvice([weak] {
            if (auto self{ weak.lock() }) {
                self->m_timer = drogon::app().getLoop()->runEvery(1.0, [weak] {
                    auto self{ weak.lock() };
                    if (!self || self->m_stopped.load()) {
                        return;
                    }
                    {
                        std::lock_guard lock{ self->m_mutex };
                        if (self->m_nextDue > common::unix_seconds()) {
                            return;
                        }
                    }
                    if (self->m_running.exchange(true)) {
                        return;
                    }
                    drogon::async_run([self]() -> drogon::Task<> { co_await self->tick(); });
                });
            }
        });
    }

    /**
     * @brief Cancel timer polling and suppress new task claims.
     */
    auto ScheduledTasks::Stop() -> void {
        m_stopped.store(true);
        if (m_timer) {
            drogon::app().getLoop()->invalidateTimer(m_timer);
        }
    }

    /**
     * @brief Observe whether an already-started poll still needs its event loop.
     * @return True until the current delivery batch and storage continuations finish.
     */
    auto ScheduledTasks::Busy() const noexcept -> bool {
        return m_running.load();
    }

    /**
     * @brief Wake the nearest-deadline timer after a persistent state change.
     */
    auto ScheduledTasks::wake() -> void {
        std::lock_guard lock{ m_mutex };
        ++m_revision;
        m_nextDue = 0;
    }

    /**
     * @brief Create one durable reminder for a private or group conversation.
     * @param peer Recipient and actual creator identity.
     * @param due_at UTC Unix seconds, within the next 366 days.
     * @param content Prompt delivered to the conversation when due.
     * @param daily Whether to repeat at the same UTC time every day.
     * @return Persisted task.
     */
    auto ScheduledTasks::Create(
        Peer peer,
        std::int64_t due_at,
        std::string content,
        bool daily
    ) -> drogon::Task<Json::Value> {
        auto const now{ common::unix_seconds() };
        auto const session{ conversation_id(peer) };
        if (session == 0 || content.empty() || content.size() > 8192 || due_at <= now || due_at > now + 366 * 86400LL) {
            throw std::invalid_argument{ "task requires a conversation, 1..8192 bytes of text, and a UTC time within the next 366 days" };
        }
        auto const rows{ co_await storage::query(m_database, "INSERT INTO scheduled_tasks (session_id, sender_id, due_at, content, daily, status, error, created_at) SELECT ?, ?, ?, ?, ?, 'pending', '', ? WHERE (SELECT count(*) FROM scheduled_tasks WHERE session_id = ? AND status = 'pending') < 100 RETURNING *", session, peer.user_id, due_at, content, daily ? 1 : 0, now, session) };
        if (rows.empty()) {
            throw std::runtime_error{ "conversation has reached the 100 pending task limit" };
        }
        wake();
        co_return task_json(rows[0]);
    }

    /**
     * @brief List bounded persistent tasks, optionally restricted to one conversation.
     * @param session Optional conversation key.
     * @return Up to 500 task records, newest first.
     */
    auto ScheduledTasks::List(std::optional<std::int64_t> session) -> drogon::Task<Json::Value> {
        auto const rows{ co_await storage::query(m_database, "SELECT * FROM scheduled_tasks WHERE (? IS NULL OR session_id = ?) ORDER BY id DESC LIMIT 500", session, session) };
        Json::Value result{ Json::arrayValue };
        for (auto const& row : rows) {
            result.append(task_json(row));
        }
        co_return result;
    }

    /**
     * @brief Cancel a pending task only if its optional conversation restriction matches.
     * @param id Task identifier.
     * @param session Optional key restricting model tools to the current conversation.
     * @return Whether a pending task was cancelled.
     */
    auto ScheduledTasks::Cancel(
        std::int64_t id,
        std::optional<std::int64_t> session
    ) -> drogon::Task<bool> {
        auto const rows{ co_await storage::query(m_database, "UPDATE scheduled_tasks SET status = 'cancelled' WHERE id = ? AND status = 'pending' AND (? IS NULL OR session_id = ?) RETURNING id", id, session, session) };
        wake();
        co_return !rows.empty();
    }

    /**
     * @brief Claim and execute bounded due work, then cache the next deadline.
     * @return Task completing after the due batch; failures are recorded or logged.
     */
    auto ScheduledTasks::tick() -> drogon::Task<> {
        std::uint64_t revision{};
        {
            std::lock_guard lock{ m_mutex };
            revision = m_revision;
        }
        try {
            auto const rows{ co_await storage::query(m_database, "SELECT * FROM scheduled_tasks WHERE status = 'pending' AND due_at <= ? ORDER BY due_at, id LIMIT 16", common::unix_seconds()) };
            for (auto const& row : rows) {
                if (m_stopped.load()) {
                    break;
                }
                auto const id{ row["id"].as<std::int64_t>() };
                auto const claimed{ co_await storage::query(m_database, "UPDATE scheduled_tasks SET status = 'running' WHERE id = ? AND status = 'pending' RETURNING id", id) };
                if (claimed.empty()) {
                    continue;
                }
                auto const session{ row["session_id"].as<std::int64_t>() };
                Peer peer{ .user_id = row["sender_id"].as<std::int64_t>(), .nickname = "scheduled task", .group_id = session < 0 ? -session : 0 };
                if (session > 0) {
                    peer.user_id = session;
                }
                std::string error{};
                try {
                    if (!m_stopped.load()) {
                        co_await m_handler(peer, row["content"].as<std::string>());
                    } else {
                        error = "stopped before delivery";
                    }
                } catch (std::exception const& exception) {
                    error = exception.what();
                }
                bool const repeat{ error.empty() && row["daily"].as<int>() != 0 };
                auto const due{ row["due_at"].as<std::int64_t>() };
                auto const next{ repeat ? due + ((common::unix_seconds() - due) / 86400 + 1) * 86400 : due };
                co_await storage::query(m_database, "UPDATE scheduled_tasks SET status = ?, error = ?, due_at = ? WHERE id = ? AND status = 'running'", error.empty() ? (repeat ? "pending" : "done") : "failed", error.substr(0, 4096), next, id);
            }
            auto const next{ co_await storage::query(m_database, "SELECT min(due_at) AS next FROM scheduled_tasks WHERE status = 'pending'") };
            std::lock_guard lock{ m_mutex };
            m_nextDue = revision != m_revision ? 0 : (next[0]["next"].isNull() ? INT64_MAX : next[0]["next"].as<std::int64_t>());
        } catch (std::exception const& error) {
            LOG_ERROR << "Polling scheduled tasks failed: " << error.what();
            std::lock_guard lock{ m_mutex };
            m_nextDue = common::unix_seconds() + 5;
        }
        m_running.store(false);
    }
} // namespace workflow
