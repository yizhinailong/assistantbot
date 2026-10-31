/**
 * @file runtime_state.hpp
 * @brief Declare the runtime state and process sampling plugin.
 */
#pragma once

#include <cstdint>
#include <deque>
#include <memory>
#include <mutex>
#include <optional>
#include <string>

#include <drogon/orm/DbClient.h>
#include <drogon/plugins/Plugin.h>
#include <drogon/utils/coroutine.h>
#include <trantor/net/EventLoop.h>

#include <drogon_onebot/onebot_client.hpp>

#include "admin/process_metrics.hpp"

namespace controllers {
    class AssistantBot;
} // namespace controllers

namespace admin {

    /**
     * @brief Hold runtime state and process samples for the administration endpoints.
     */
    class Runtime : public drogon::Plugin<Runtime> {
    private:
        /**
         * @brief State shared with the periodic timer.
         */
        struct State {
            std::mutex mutex;                  ///< Guards `samples`.
            std::deque<ProcessMetric> samples; ///< Oldest sample first.
        };

        controllers::AssistantBot* m_bot{};                  ///< Bot plugin whose pause and session state are reported.
        std::optional<drogon_onebot::OneBot> m_onebot;       ///< Typed client for runtime OneBot details.
        drogon::orm::DbClientPtr m_database;                 ///< Database for daily model usage.
        std::string m_model;                                 ///< Configured model name.
        std::int64_t m_started_at{};                         ///< Unix timestamp when this plugin initialized.
        std::shared_ptr<State> m_state;                      ///< Metrics state shared with the timer callback.
        trantor::EventLoop* m_loop{};                        ///< Main application loop that owns `m_timer`.
        trantor::TimerId m_timer{ trantor::InvalidTimerId }; ///< Periodic metrics timer.

        /**
         * @brief Append one sample and enforce the retention capacity.
         * @param state Shared metrics state.
         */
        static auto appendSample(std::shared_ptr<State> const& state) -> void;

    public:
        /**
         * @brief Bind dependencies, record startup, and start process sampling.
         * @param config Plugin config; currently unused.
         * @throws std::invalid_argument If a required dependency is missing.
         */
        auto initAndStart(Json::Value const& config) -> void override;

        /**
         * @brief Invalidate the process sampler and release shared state.
         */
        auto shutdown() -> void override;

        /**
         * @brief Build the runtime status, querying independent OneBot details asynchronously.
         * @return Status JSON with null partial values when an upstream action fails.
         */
        [[nodiscard]] auto Status() const -> drogon::Task<Json::Value>;

        /**
         * @brief Return whether the bot drops new friend messages.
         * @return Current pause flag.
         */
        [[nodiscard]] auto Paused() const noexcept -> bool;

        /**
         * @brief Pause or resume new friend messages.
         * @param paused Whether to drop new messages before commands or queuing.
         */
        auto SetPaused(bool paused) noexcept -> void;

        /**
         * @brief Copy retained process resource samples.
         * @return Samples in chronological order.
         */
        [[nodiscard]] auto ProcessMetrics() const -> Json::Value;
    };

} // namespace admin
