/**
 * @file assistant_bot.hpp
 * @brief Declare the service that turns the OneBot and OpenAI plugins into a private chat bot.
 */
#pragma once

#include <atomic>
#include <cstddef>
#include <memory>

#include <drogon/orm/DbClient.h>
#include <json/value.h>

namespace drogon_onebot {
    class OneBotClient;
} // namespace drogon_onebot

namespace drogon_openai {
    class OpenAIClient;
} // namespace drogon_openai

namespace workflow {

    class AccessControl;
    class BotSettings;
    class FeatureSettings;
    class ModelRoles;
    class MemoryService;
    class ScheduledTasks;
    class PrivateChat;

} // namespace workflow

namespace controllers {

    constexpr char const* HELP_COMMAND{ "/help" };   ///< Message that asks for `HELP_REPLY`.
    constexpr char const* RESET_COMMAND{ "/reset" }; ///< Message that clears the sender's history.

    /**
     * @brief Answer to `HELP_COMMAND`.
     */
    constexpr char const* HELP_REPLY{ "直接发消息就能和我聊天。\n/reset 清空对话历史\n/help 显示这条帮助" };

    /**
     * @brief Answer to `RESET_COMMAND`.
     */
    constexpr char const* RESET_REPLY{ "对话历史已清空。" };

    /**
     * @brief Answer to a message without text, such as a lone image.
     */
    constexpr char const* TEXT_ONLY_REPLY{ "目前只支持文字消息。" };

    /**
     * @brief Answer the private messages of QQ friends with an OpenAI-compatible model.
     *
     * `app::Application` supplies the initialized OneBot and OpenAI plugins and database.
     * The OneBot plugin needs an `event_path`, or no message reaches the bot.
     * `workflow::parse_bot_config()` describes `config.bot`, and `workflow::BotSettings`
     * applies saved overrides. `workflow::AccessControl` persists the mode and lists.
     */
    class AssistantBot {
    private:
        std::shared_ptr<workflow::PrivateChat> m_chat;         ///< Handler state, shared with the registered message handler.
        std::shared_ptr<std::atomic_bool> m_paused;            ///< Pause flag shared with the registered message handler.
        std::shared_ptr<workflow::BotSettings> m_settings;     ///< Live reply settings shared with the workflow.
        std::shared_ptr<workflow::ScheduledTasks> m_tasks;     ///< Durable reminder service.
        std::shared_ptr<workflow::ModelRoles> m_models;        ///< Shared independent model endpoints.
        std::shared_ptr<workflow::MemoryService> m_memory;     ///< Persistent optional memory service.
        std::shared_ptr<workflow::FeatureSettings> m_features; ///< Optional capabilities and group settings.
        std::shared_ptr<workflow::AccessControl> m_access;     ///< Persistent policy shared with the registered message handler.

    public:
        /**
         * @brief Read the settings, and register a message handler with the OneBot plugin.
         *
         * The handler logs every message event at INFO, group messages included, and ignores
         * all but private friend messages allowed by the access policy. It answers `HELP_COMMAND`,
         * `RESET_COMMAND`, and a message without text itself, and passes any other text to
         * `workflow::PrivateChat::Chat`.
         * @param config Application `bot` settings.
         * @param onebot Initialized OneBot plugin for message registration.
         * @param openai Initialized OpenAI plugin for model clients.
         * @param database Opened and migrated SQLite client.
         * @throws std::invalid_argument If the config, saved overrides, or access policy are invalid.
         * @throws std::runtime_error If the saved overrides cannot be read.
         */
        auto Start(
            Json::Value const& config,
            drogon_onebot::OneBotClient& onebot,
            drogon_openai::OpenAIClient& openai,
            drogon::orm::DbClientPtr database
        ) -> void;

        /**
         * @brief Report whether new friend messages are paused.
         * @return True when new messages are dropped before commands or queuing.
         */
        [[nodiscard]] auto Paused() const noexcept -> bool;

        /**
         * @brief Pause or resume handling of new friend messages.
         * @param paused Whether new messages should be dropped.
         * @note Replies already in flight continue to completion.
         */
        auto SetPaused(bool paused) noexcept -> void;

        /**
         * @brief Count the users whose chat session state has been created.
         * @return Number of sessions, or zero before initialization or after shutdown.
         */
        [[nodiscard]] auto SessionCount() const -> std::size_t;

        /**
         * @brief Get the access policy shared with message handlers.
         * @return Policy manager, or null before initialization or after shutdown.
         */
        [[nodiscard]] auto Access() const -> std::shared_ptr<workflow::AccessControl>;

        /**
         * @brief Get the persistent reply settings shared with the workflow.
         * @return Settings manager during the application lifetime.
         */
        [[nodiscard]] auto Settings() const -> std::shared_ptr<workflow::BotSettings>;

        /**
         * @brief Get the shared chat workflow for authenticated session inspection and reset.
         * @return Workflow during the application lifetime.
         */
        [[nodiscard]] auto Chat() const -> std::shared_ptr<workflow::PrivateChat>;

        /**
         * @brief Get the immutable optional-feature settings manager.
         * @return Settings manager during the application lifetime.
         */
        [[nodiscard]] auto Features() const -> std::shared_ptr<workflow::FeatureSettings>;

        /**
         * @brief Get the persistent memory service used by the bot and admin endpoints.
         * @return Memory service during application lifetime.
         */
        [[nodiscard]] auto Memory() const -> std::shared_ptr<workflow::MemoryService>;

        /**
         * @brief Get the durable conversation reminder service.
         * @return Task service during application lifetime.
         */
        [[nodiscard]] auto Tasks() const -> std::shared_ptr<workflow::ScheduledTasks>;

        /**
         * @brief Drop the service's reference to the handler state.
         *
         * Handlers that are still running keep their own reference until they end.
         */
        auto Stop() -> void;
    };

} // namespace controllers
