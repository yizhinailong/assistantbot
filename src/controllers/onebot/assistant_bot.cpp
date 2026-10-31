/**
 * @file assistant_bot.cpp
 * @brief Implement the plugin that turns the OneBot and OpenAI plugins into a private chat bot.
 */
#include "controllers/onebot/assistant_bot.hpp"

#include <stdexcept>
#include <utility>

#include <drogon/drogon.h>

#include <drogon_onebot/onebot_client.hpp>
#include <drogon_openai/openai_client.hpp>

#include "admin/http_traces.hpp"
#include "common/text.hpp"
#include "storage/database.hpp"
#include "workflow/access_control.hpp"
#include "workflow/bot_settings.hpp"
#include "workflow/private_chat.hpp"

namespace controllers {

    namespace {

        /**
         * @brief Log a message event, and answer it when it is a private message from a friend.
         *
         * Ignores group messages, temporary sessions, and friends excluded by the policy.
         * Answers `HELP_COMMAND` and `RESET_COMMAND` directly, and a message without text
         * with `TEXT_ONLY_REPLY`. Any other text goes to `workflow::PrivateChat::Chat`.
         * @param chat Workflow that answers the friends.
         * @param paused Pause flag checked before commands or queuing.
         * @param access Persistent policy checked before commands or queuing.
         * @param event Event from the OneBot implementation.
         * @return Task that ends when every reply that this call is responsible for has been sent.
         */
        auto handle_message(
            std::shared_ptr<workflow::PrivateChat> chat,
            std::shared_ptr<std::atomic_bool> paused,
            std::shared_ptr<workflow::AccessControl> access,
            drogon_onebot::MessageEvent event
        ) -> drogon::Task<> {
            LOG_INFO << common::describe_received(event);
            if (event.message_type != "private" || event.sub_type != "friend") {
                co_return;
            }
            workflow::Peer const peer{ .self_id = event.self_id, .user_id = event.user_id, .nickname = std::move(event.sender.nickname) };
            auto const policy{ access->Current() };
            std::string ignored_reason{};
            if (paused->load(std::memory_order_relaxed)) {
                ignored_reason = "paused";
            } else if (!policy->Allows(event.user_id)) {
                ignored_reason = policy->blacklist.contains(event.user_id) ? "blacklist" : "outside_allowlist";
            }
            std::string content{};
            for (auto const& segment : event.message) {
                content += segment.type == "text" && segment.data["text"].isString() ? segment.data["text"].asString() : "[" + segment.type + "]";
            }
            co_await chat->Observe(peer, std::move(content), ignored_reason);
            if (!ignored_reason.empty()) {
                co_return;
            }
            auto text{ common::extract_text(event.message) };
            if (text.empty()) {
                co_await chat->Send(peer, TEXT_ONLY_REPLY);
                co_return;
            }
            if (text == HELP_COMMAND) {
                co_await chat->Send(peer, HELP_REPLY);
                co_return;
            }
            if (text == RESET_COMMAND) {
                chat->Reset(peer.user_id);
                co_await chat->Send(peer, RESET_REPLY);
                co_return;
            }
            co_await chat->Chat(peer, std::move(text));
        }

    } // namespace

    /**
     * @brief Read the settings, and register a message handler with the OneBot plugin.
     *
     * The handler logs every message event at INFO, group messages included, and ignores
     * all but private friend messages allowed by the access policy. It answers `HELP_COMMAND`,
     * `RESET_COMMAND`, and a message without text itself, and passes any other text to
     * `workflow::PrivateChat::Chat`.
     * @param config The plugin's `config` object.
     * @throws std::invalid_argument If the config, saved overrides, or access policy are invalid, or a
     * dependency is not loaded.
     * @throws std::runtime_error If the saved overrides cannot be read.
     */
    auto AssistantBot::initAndStart(Json::Value const& config) -> void {
        auto* const onebot{ drogon::app().getPlugin<drogon_onebot::OneBotClient>() };
        auto* const openai{ drogon::app().getPlugin<drogon_openai::OpenAIClient>() };
        auto* const database{ drogon::app().getPlugin<storage::Database>() };
        if (onebot == nullptr || openai == nullptr || database == nullptr) {
            throw std::invalid_argument{ "controllers::AssistantBot: drogon_onebot::OneBotClient, drogon_openai::OpenAIClient, and storage::Database must be loaded and listed under dependencies" };
        }

        // Drogon starts the plugins before any event loop runs, so reading the database blocks safely.
        m_settings = std::make_shared<workflow::BotSettings>(config, database->Client());
        m_chat = std::make_shared<workflow::PrivateChat>(m_settings, admin::traced_onebot(), admin::traced_model(openai->NewOpenAIClient()), database->Client(), openai->Config().model);
        m_paused = std::make_shared<std::atomic_bool>(false);
        m_access = std::make_shared<workflow::AccessControl>(database->Client());
        onebot->OnMessage([chat = m_chat, paused = m_paused, access = m_access](drogon_onebot::MessageEvent event) -> drogon::Task<> {
            co_await handle_message(chat, paused, access, std::move(event));
        });
    }

    /**
     * @brief Report whether new friend messages are paused.
     * @return True when new messages are dropped before commands or queuing.
     */
    auto AssistantBot::Paused() const noexcept -> bool {
        return m_paused != nullptr && m_paused->load(std::memory_order_relaxed);
    }

    /**
     * @brief Pause or resume handling of new friend messages.
     * @param paused Whether new messages should be dropped.
     * @note Replies already in flight continue to completion.
     */
    auto AssistantBot::SetPaused(bool paused) noexcept -> void {
        if (m_paused != nullptr) {
            m_paused->store(paused, std::memory_order_relaxed);
        }
    }

    /**
     * @brief Count the users whose chat session state has been created.
     * @return Number of sessions, or zero before initialization or after shutdown.
     */
    auto AssistantBot::SessionCount() const -> std::size_t {
        return m_chat != nullptr ? m_chat->SessionCount() : 0;
    }

    /**
     * @brief Get the access policy shared with message handlers.
     * @return Policy manager, or null before initialization or after shutdown.
     */
    auto AssistantBot::Access() const -> std::shared_ptr<workflow::AccessControl> {
        return m_access;
    }

    /**
     * @brief Drop the plugin's reference to the handler state.
     *
     * Handlers that are still running keep their own reference until they end.
     */
    auto AssistantBot::shutdown() -> void {
        m_settings.reset();
        m_access.reset();
        m_paused.reset();
        m_chat.reset();
    }

    /**
     * @brief Get the persistent reply settings shared with the workflow.
     * @return Settings manager during the plugin lifetime.
     */
    auto AssistantBot::Settings() const -> std::shared_ptr<workflow::BotSettings> {
        return m_settings;
    }

    /**
     * @brief Get the shared chat workflow for authenticated session inspection and reset.
     * @return Workflow during the plugin lifetime.
     */
    auto AssistantBot::Chat() const -> std::shared_ptr<workflow::PrivateChat> {
        return m_chat;
    }

} // namespace controllers
