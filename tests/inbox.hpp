/**
 * @file inbox.hpp
 * @brief Share a bounded-wait thread-safe inbox for asynchronous fake service requests.
 */
#pragma once
#include <chrono>
#include <condition_variable>
#include <deque>
#include <mutex>
#include <optional>

namespace test_support {
    /**
     * @brief Transfer observed requests from app IO loops to the test thread.
     * @tparam T Owned request value.
     */
    template <typename T>
    class Inbox {
    private:
        std::mutex m_mutex;              ///< Guards queued values.
        std::condition_variable m_ready; ///< Wakes readers after a push.
        std::deque<T> m_items;           ///< Pending observations.

    public:
        /**
         * @brief Queue an owned observation and wake a reader.
         * @param value Observed request.
         */
        auto Push(T value) -> void {
            {
                std::lock_guard lock{ m_mutex };
                m_items.push_back(std::move(value));
            }
            m_ready.notify_all();
        }

        /**
         * @brief Read the oldest observation, waiting at most two seconds.
         * @return Request or empty on timeout.
         */
        auto Pop() -> std::optional<T> {
            std::unique_lock lock{ m_mutex };
            if (!m_ready.wait_for(lock, std::chrono::seconds{ 2 }, [this] { return !m_items.empty(); })) {
                return {};
            }
            auto value{ std::move(m_items.front()) };
            m_items.pop_front();
            return value;
        }
    };
} // namespace test_support
