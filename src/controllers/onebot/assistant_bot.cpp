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
#include "workflow/feature_settings.hpp"
#include "workflow/memory.hpp"
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
            std::shared_ptr<workflow::FeatureSettings> features,
            drogon_onebot::MessageEvent event
        ) -> drogon::Task<> {
            LOG_INFO << common::describe_received(event);
            bool const group_message{ event.message_type == "group" && event.group_id && *event.group_id > 0 };
            if (!group_message && (event.message_type != "private" || event.sub_type != "friend")) {
                co_return;
            }
            auto const settings{ features->Current() };
            auto group{ group_message ? (*settings)["groups"][std::to_string(*event.group_id)] : Json::Value{} };
            bool administrator{};
            for (auto const& id : (*settings)["admins"]) {
                administrator = administrator || id.asString() == std::to_string(event.user_id);
            }
            workflow::Peer const peer{
                .self_id = event.self_id,
                .user_id = event.user_id,
                .nickname = event.sender.nickname,
                .group_id = group_message ? *event.group_id : 0,
                .additional_prompt = group.get("additional_prompt", "").asString(),
            };
            auto const policy{ access->Current() };
            auto text{ common::extract_text(event.message) };
            bool addressed{};
            for (auto const& segment : event.message) {
                if (segment.type == "at" && segment.data["qq"].asString() == std::to_string(event.self_id)) {
                    addressed = true;
                }
            }
            std::string ignored_reason{};
            if (paused->load(std::memory_order_relaxed)) {
                ignored_reason = "paused";
            } else if (policy->blacklist.contains(event.user_id)) {
                ignored_reason = "blacklist";
            } else if (!group_message && !policy->Allows(event.user_id)) {
                ignored_reason = "outside_allowlist";
            } else if (group_message && text.starts_with('/')) {
                if (!administrator) {
                    ignored_reason = "administrator_required";
                } else if (!group.get("commands_enabled", true).asBool()) {
                    ignored_reason = "group_commands_disabled";
                }
            } else if (group_message && !group.get("enabled", false).asBool()) {
                ignored_reason = "group_disabled";
            } else if (group_message && !group.get("auto_reply", false).asBool() && !addressed) {
                ignored_reason = "not_addressed";
            }
            std::string content{};
            for (auto const& segment : event.message) {
                content += segment.type == "text" && segment.data["text"].isString() ? segment.data["text"].asString() : "[" + segment.type + "]";
            }
            co_await chat->Observe(peer, std::move(content), ignored_reason);
            if (!ignored_reason.empty()) {
                co_return;
            }
            if (group_message && text.starts_with('/')) {
                bool changed{};
                if (text == "/on" || text == "/off") {
                    group["enabled"] = text == "/on";
                    changed = true;
                } else if (text == "/auto on" || text == "/auto off") {
                    group["auto_reply"] = text == "/auto on";
                    changed = true;
                } else if (text.starts_with("/prompt ")) {
                    group["additional_prompt"] = text.substr(8);
                    changed = true;
                }
                if (changed) {
                    auto groups{ (*settings)["groups"] };
                    groups[std::to_string(peer.group_id)] = std::move(group);
                    std::string result{ "群设置已保存。" };
                    try {
                        co_await features->Update("groups", std::move(groups), (*settings)["_revision"].asUInt64());
                    } catch (std::exception const& error) {
                        result = error.what();
                    }
                    co_await chat->Send(peer, std::move(result));
                    co_return;
                }
                if (!group.get("enabled", false).asBool()) {
                    co_return;
                }
            }
            if (text.empty()) {
                co_await chat->Send(peer, TEXT_ONLY_REPLY);
                co_return;
            }
            if (text == HELP_COMMAND) {
                co_await chat->Send(peer, HELP_REPLY);
                co_return;
            }
            if (text == RESET_COMMAND) {
                chat->Reset(workflow::conversation_id(peer));
                co_await chat->Send(peer, RESET_REPLY);
                co_return;
            }
            if (group_message && group.get("include_card", true).asBool()) {
                auto const name{ event.sender.card.empty() ? event.sender.nickname : event.sender.card };
                text = "[" + name + " QQ:" + std::to_string(peer.user_id) + "] " + text;
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
        m_features = std::make_shared<workflow::FeatureSettings>(database->Client());
        m_models = std::make_shared<workflow::ModelRoles>(openai->NewOpenAIClient(), database->Client(), openai->Config().model);
        m_memory = std::make_shared<workflow::MemoryService>(database->Client(), m_models);
        m_settings = std::make_shared<workflow::BotSettings>(config, database->Client());
        m_chat = std::make_shared<workflow::PrivateChat>(m_settings, admin::traced_onebot(), admin::traced_model(openai->NewOpenAIClient()), database->Client(), openai->Config().model);
        m_paused = std::make_shared<std::atomic_bool>(false);
        m_access = std::make_shared<workflow::AccessControl>(database->Client());
        onebot->OnMessage([chat = m_chat, paused = m_paused, access = m_access, features = m_features](drogon_onebot::MessageEvent event) -> drogon::Task<> {
            co_await handle_message(chat, paused, access, features, std::move(event));
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
        m_memory.reset();
        m_models.reset();
        m_features.reset();
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

    /**
     * @brief Get the immutable optional-feature settings manager.
     * @return Settings manager during the plugin lifetime.
     */
    auto AssistantBot::Features() const -> std::shared_ptr<workflow::FeatureSettings> {
        return m_features;
    }

    /**
     * @brief Get the persistent memory service used by the bot and admin endpoints.
     * @return Memory service during plugin lifetime.
     */
    auto AssistantBot::Memory() const -> std::shared_ptr<workflow::MemoryService> {
        return m_memory;
    }

} // namespace controllers
