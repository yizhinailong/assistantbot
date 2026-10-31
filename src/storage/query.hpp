/**
 * @file query.hpp
 * @brief Keep SQLite coroutine continuations on their caller's event loop.
 */
#pragma once

#include <exception>
#include <optional>
#include <string>
#include <utility>

#include <drogon/orm/DbClient.h>
#include <drogon/utils/coroutine.h>
#include <trantor/net/EventLoop.h>

namespace storage {

    /**
     * @brief Execute SQL and return to the caller's event loop on success or failure.
     *
     * Drogon's SQLite awaiter resumes on the connection's own thread. Continuing a chat
     * there also moves its HTTP clients and final database ownership there, which can
     * make a connection destroy and join its own thread at shutdown. Queue the continuation
     * onto the original loop before returning or rethrowing. Callers without a running loop
     * retain the SQLite completion thread.
     * @tparam Args Bound parameter types.
     * @param db Database client.
     * @param sql One statement, retained throughout execution.
     * @param parameters Bound values, owned by the coroutine.
     * @return Immutable query result.
     * @throws drogon::orm::DrogonDbException If execution fails.
     * @note Keep the caller's event loop running until its storage tasks finish.
     */
    template <typename... Args>
    auto query(
        drogon::orm::DbClientPtr db,
        std::string sql,
        Args... parameters
    ) -> drogon::Task<drogon::orm::Result> {
        auto* const current_loop{ trantor::EventLoop::getEventLoopOfCurrentThread() };
        auto* const caller_loop{ current_loop != nullptr && current_loop->isRunning() ? current_loop : nullptr };
        std::optional<drogon::orm::Result> result{};
        std::exception_ptr error{};
        try {
            result = co_await db->execSqlCoro(sql, std::move(parameters)...);
        } catch (...) {
            error = std::current_exception();
        }
        if (caller_loop != nullptr) {
            co_await drogon::queueInLoopCoro(caller_loop, [] {});
        }
        if (error) {
            std::rethrow_exception(error);
        }
        co_return std::move(*result);
    }

} // namespace storage
