/**
 * @file runtime_state.cpp
 * @brief Implement runtime state, daily usage, and process sampling.
 */
#include "admin/runtime_state.hpp"

#include <chrono>
#include <stdexcept>
#include <tuple>
#include <utility>

#include <drogon/drogon.h>

#include <drogon_openai/openai_client.hpp>

#include "admin/http_traces.hpp"
#include "controllers/onebot/assistant_bot.hpp"
#include "storage/database.hpp"
#include "storage/usage.hpp"
#include "workflow/bot_settings.hpp"

namespace admin {

    namespace {
        constexpr double STATUS_TIMEOUT_SECONDS{ 2.0 }; ///< Bound each independent OneBot query.
    } // namespace

    /**
     * @brief Append one sample and enforce the retention capacity.
     * @param state Shared metrics state.
     */
    auto Runtime::appendSample(std::shared_ptr<State> const& state) -> void {
        std::lock_guard lock{ state->mutex };
        state->samples.push_back(sample_process_metrics());
        while (state->samples.size() > PROCESS_METRICS_CAPACITY) {
            state->samples.pop_front();
        }
    }

    /**
     * @brief Bind dependencies, record startup, and start process sampling.
     * @param config Plugin config; currently unused.
     * @throws std::invalid_argument If a required dependency is missing.
     */
    auto Runtime::initAndStart(Json::Value const& /*config*/) -> void {
        m_bot = drogon::app().getPlugin<controllers::AssistantBot>();
        auto* const onebot{ drogon::app().getPlugin<drogon_onebot::OneBotClient>() };
        auto* const openai{ drogon::app().getPlugin<drogon_openai::OpenAIClient>() };
        auto* const database{ drogon::app().getPlugin<storage::Database>() };
        if (m_bot == nullptr || onebot == nullptr || openai == nullptr || database == nullptr) {
            throw std::invalid_argument{ "admin::Runtime: controllers::AssistantBot, drogon_onebot::OneBotClient, drogon_openai::OpenAIClient, and storage::Database must be loaded and listed under dependencies" };
        }
        m_database = database->Client();
        m_onebot = admin::traced_onebot();
        m_model = openai->Config().model;
        m_started_at = std::chrono::duration_cast<std::chrono::seconds>(
                           std::chrono::system_clock::now().time_since_epoch()
        )
                           .count();
        m_state = std::make_shared<State>();
        appendSample(m_state);
        m_loop = drogon::app().getLoop();
        std::weak_ptr<State> const weak_state{ m_state };
        m_timer = m_loop->runEvery(PROCESS_METRICS_INTERVAL_SECONDS, [weak_state] {
            if (auto state{ weak_state.lock() }) {
                Runtime::appendSample(state);
            }
        });
    }

    /**
     * @brief Invalidate the process sampler and release shared state.
     */
    auto Runtime::shutdown() -> void {
        if (m_loop != nullptr && m_timer != trantor::InvalidTimerId) {
            m_loop->invalidateTimer(m_timer);
        }
        m_timer = trantor::InvalidTimerId;
        m_loop = nullptr;
        m_state.reset();
        m_bot = nullptr;
    }

    /**
     * @brief Build the runtime status, querying independent OneBot details asynchronously.
     * @return Status JSON with null partial values when an upstream action fails.
     */
    auto Runtime::Status() const -> drogon::Task<Json::Value> {
        auto const [login, status, version]{ co_await drogon::when_all(
            m_onebot->GetLoginInfo({ .timeout = STATUS_TIMEOUT_SECONDS }),
            m_onebot->GetStatus({ .timeout = STATUS_TIMEOUT_SECONDS }),
            m_onebot->GetVersionInfo({ .timeout = STATUS_TIMEOUT_SECONDS })
        ) };

        Json::Value body{ Json::objectValue };
        auto const today{ std::chrono::floor<std::chrono::days>(std::chrono::system_clock::now()) };
        auto const from{ std::chrono::duration_cast<std::chrono::seconds>(today.time_since_epoch()).count() };
        body["today_usage"] = co_await storage::summarize_usage(m_database, from, from + 86400);
        body["version"] = ASSISTANTBOT_VERSION;
        body["started_at"] = Json::Int64{ m_started_at };
        auto const settings{ m_bot->Settings()->Current() };
        body["model"] = settings->model.empty() ? m_model : settings->model;
        body["session_count"] = Json::UInt64{ m_bot != nullptr ? m_bot->SessionCount() : 0 };
        body["paused"] = Paused();
        body["bot"] = Json::Value{};
        if (login) {
            Json::Value bot{ Json::objectValue };
            bot["user_id"] = std::to_string(login->user_id);
            bot["nickname"] = login->nickname;
            body["bot"] = std::move(bot);
        }
        Json::Value napcat{ Json::objectValue };
        napcat["online"] = Json::Value{};
        napcat["good"] = Json::Value{};
        napcat["app_name"] = Json::Value{};
        napcat["app_version"] = Json::Value{};
        napcat["protocol_version"] = Json::Value{};
        if (status) {
            if (status->online) {
                napcat["online"] = *status->online;
            }
            napcat["good"] = status->good;
        }
        if (version) {
            napcat["app_name"] = version->app_name;
            napcat["app_version"] = version->app_version;
            napcat["protocol_version"] = version->protocol_version;
        }
        body["napcat"] = std::move(napcat);
        co_return body;
    }

    /**
     * @brief Return whether the bot drops new friend messages.
     * @return Current pause flag.
     */
    auto Runtime::Paused() const noexcept -> bool {
        return m_bot != nullptr && m_bot->Paused();
    }

    /**
     * @brief Pause or resume new friend messages.
     * @param paused Whether to drop new messages before commands or queuing.
     */
    auto Runtime::SetPaused(bool paused) noexcept -> void {
        if (m_bot != nullptr) {
            m_bot->SetPaused(paused);
        }
    }

    /**
     * @brief Copy retained process resource samples.
     * @return Samples in chronological order.
     */
    auto Runtime::ProcessMetrics() const -> Json::Value {
        Json::Value body{ Json::objectValue };
        body["interval_seconds"] = PROCESS_METRICS_INTERVAL_SECONDS;
        body["capacity"] = Json::UInt64{ PROCESS_METRICS_CAPACITY };
        body["samples"] = Json::Value{ Json::arrayValue };
        if (m_state == nullptr) {
            return body;
        }
        std::lock_guard lock{ m_state->mutex };
        for (auto const& sample : m_state->samples) {
            body["samples"].append(process_metric_json(sample));
        }
        return body;
    }

} // namespace admin
