/**
 * @file private_chat.cpp
 * @brief Implement the workflow that answers private messages with the model.
 */
#include "workflow/private_chat.hpp"

#include <utility>

#include <drogon/drogon.h>

#include "admin/live_events.hpp"
#include "common/text.hpp"
#include "common/time.hpp"
#include "storage/messages.hpp"
#include "storage/usage.hpp"

namespace workflow {

    namespace {

        /**
         * @brief Turn a completion into the text to send, logging why there is none.
         * @param completion Result of the model request.
         * @param user_id QQ number of the user, for the log.
         * @return The reply without Markdown markup; empty when the request failed or the
         * reply has no text.
         */
        auto reply_text(
            drogon_openai::Result<drogon_openai::ChatCompletion> const& completion,
            std::int64_t user_id
        ) -> std::string {
            if (!completion) {
                LOG_ERROR << "Chat completion for " << user_id << " failed: " << completion.error().message;
                return {};
            }
            if (completion->choices.empty() || !completion->choices.front().message.content) {
                LOG_ERROR << "Chat completion for " << user_id << " has no reply text";
                return {};
            }
            auto text{ common::strip_markdown(*completion->choices.front().message.content) };
            if (text.empty()) {
                LOG_ERROR << "Chat completion for " << user_id << " has a blank reply";
            }
            return text;
        }

    } // namespace

    /**
     * @brief Bind the workflow to its settings and clients.
     * @param settings Settings of the bot, shared with whoever updates them.
     * @param onebot Client for the OneBot implementation's HTTP API.
     * @param openai Client for the model's API.
     * @param database Database for completed model usage.
     * @param model Configured model name for failed requests.
     */
    PrivateChat::PrivateChat(
        std::shared_ptr<BotSettings const> settings,
        drogon_onebot::OneBot onebot,
        drogon_openai::OpenAI openai,
        drogon::orm::DbClientPtr database,
        std::string model
    )
        : m_settings{ std::move(settings) },
          m_onebot{ std::move(onebot) },
          m_openai{ std::move(openai) },
          m_sessions{ m_settings },
          m_database{ std::move(database) },
          m_model{ std::move(model) } {}

    /**
     * @brief Answer a text message of a friend with the model.
     *
     * While the friend waits for a reply, their further messages are queued, and the call
     * that is answering sends them to the model together afterwards.
     * @param peer Friend who sent the message.
     * @param text Message text; not empty.
     * @return Task that ends when every reply that this call is responsible for has been sent.
     */
    auto PrivateChat::Chat(Peer peer, std::string text) -> drogon::Task<> {
        auto const claimed{ m_sessions.Enqueue(peer.user_id, std::move(text), SessionStore::Clock::now()) };
        notifySession(peer.user_id);
        if (!claimed) {
            co_return;
        }
        try {
            while (auto prompt{ m_sessions.TakePending(peer.user_id) }) {
                co_await answer(peer, std::move(*prompt));
            }
            notifySession(peer.user_id);
        } catch (...) {
            // A claim left behind would queue this user's messages forever.
            m_sessions.Release(peer.user_id);
            notifySession(peer.user_id);
            throw;
        }
    }

    /**
     * @brief Forget a friend's history and queued messages.
     *
     * A reply that is being generated is still sent, but not recorded.
     * @param user_id QQ number of the friend.
     */
    auto PrivateChat::Reset(std::int64_t user_id) -> void {
        m_sessions.Reset(user_id);
        notifySession(user_id);
    }

    /**
     * @brief Count the users whose session state has been created.
     * @return Number of sessions under the store lock.
     */
    auto PrivateChat::SessionCount() const -> std::size_t {
        return m_sessions.Count();
    }

    /**
     * @brief Ask the model about a message, then record and send the reply.
     *
     * Sends `FAILURE_REPLY` instead, and adds no context turn, when the model gives no reply.
     * @param peer Friend who asked.
     * @param prompt User message, possibly several queued ones joined by line breaks.
     * @return Task that ends when the reply has been sent.
     */
    auto PrivateChat::answer(Peer peer, std::string prompt) -> drogon::Task<> {
        auto const config{ m_settings->Current() };
        auto const history{ m_sessions.History(peer.user_id) };
        drogon_openai::ChatCompletionCreateParams params{};
        if (!config->system_prompt.empty()) {
            params.messages.push_back({ .role = "system", .content = config->system_prompt });
        }
        for (auto const& turn : history.turns) {
            params.messages.push_back({ .role = "user", .content = turn.user });
            params.messages.push_back({ .role = "assistant", .content = turn.assistant });
        }
        params.messages.push_back({ .role = "user", .content = prompt });
        if (!config->model.empty()) {
            params.model = config->model;
        }
        params.max_tokens = config->max_tokens;
        params.timeout = config->request_timeout;

        auto const completion{ co_await m_openai.chat.completions.Create(std::move(params)) };
        auto reply{ reply_text(completion, peer.user_id) };
        storage::UsageRecord usage{
            .timestamp = std::chrono::duration_cast<std::chrono::seconds>(std::chrono::system_clock::now().time_since_epoch()).count(),
            .user_id = peer.user_id,
            .model = completion ? completion->model : (config->model.empty() ? m_model : config->model),
            .succeeded = !reply.empty(),
        };
        if (completion && completion->usage) {
            usage.prompt_tokens = completion->usage->prompt_tokens;
            usage.completion_tokens = completion->usage->completion_tokens;
            usage.total_tokens = completion->usage->total_tokens;
            if (completion->usage->prompt_tokens_details) {
                usage.cached_tokens = completion->usage->prompt_tokens_details->cached_tokens;
            }
            if (!usage.cached_tokens && completion->usage->raw["prompt_cache_hit_tokens"].isInt64()) {
                usage.cached_tokens = completion->usage->raw["prompt_cache_hit_tokens"].asInt64();
            }
            if (usage.cached_tokens && *usage.cached_tokens < 0) {
                usage.cached_tokens.reset();
            }
        }
        try {
            co_await storage::record_usage(m_database, std::move(usage));
            Json::Value event{ Json::objectValue };
            event["type"] = "usage_updated";
            event["user_id"] = std::to_string(peer.user_id);
            admin::publish_event(std::move(event));
        } catch (std::exception const& error) {
            LOG_ERROR << "Recording model usage failed: " << error.what();
        }
        if (reply.empty()) {
            co_await Send(peer, FAILURE_REPLY);
            co_return;
        }
        auto parts{ common::split_reply(reply, config->max_reply_chars) };
        m_sessions.Record(peer.user_id, history.generation, { .user = std::move(prompt), .assistant = std::move(reply) }, SessionStore::Clock::now());
        for (auto& part : parts) {
            co_await Send(peer, std::move(part));
        }
    }

    /**
     * @brief Send a text message to a friend, and log it at INFO, or the failure at ERROR.
     * @param peer Recipient.
     * @param text Text to send.
     * @return Task that ends when the OneBot implementation has answered.
     */
    auto PrivateChat::Send(Peer peer, std::string text) -> drogon::Task<> {
        auto const sent{ co_await m_onebot.SendPrivateMsg({
            .user_id = peer.user_id,
            .message = { drogon_onebot::MessageSegment::Text(text) },
        }) };
        if (!sent) {
            LOG_ERROR << "Sending a message to " << peer.user_id << " failed: " << sent.error().message;
            co_await appendMessage(peer, "outgoing", std::move(text), "failed", sent.error().message);
            co_return;
        }
        LOG_INFO << common::describe_sent(peer.self_id, peer.user_id, peer.nickname, text);
        co_await appendMessage(peer, "outgoing", std::move(text), "sent", {});
    }

    /**
     * @brief Append an incoming friend message, including ignored messages.
     * @param peer Sender identity.
     * @param content Text and readable media placeholders.
     * @param ignored_reason Empty for an accepted message, otherwise its exclusion reason.
     * @return Task that completes after the append attempt.
     */
    auto PrivateChat::Observe(
        Peer peer,
        std::string content,
        std::string ignored_reason
    ) -> drogon::Task<> {
        if (ignored_reason.empty()) {
            m_sessions.Touch(peer.user_id, peer.nickname, common::unix_seconds());
        }
        co_await appendMessage(peer, "incoming", std::move(content), ignored_reason.empty() ? "received" : "ignored", ignored_reason);
    }

    /**
     * @brief Copy metadata of in-memory sessions.
     * @return Independent session summaries.
     */
    auto PrivateChat::Sessions() const -> std::vector<SessionSummary> {
        return m_sessions.Summaries();
    }

    /**
     * @brief Copy one in-memory conversation without creating it.
     * @param user_id Friend QQ number.
     * @return Session snapshot or empty.
     */
    auto PrivateChat::Session(std::int64_t user_id) const -> std::optional<SessionSnapshot> {
        return m_sessions.Detail(user_id);
    }

    /**
     * @brief Notify session subscribers that metadata or context changed.
     * @param user_id Friend QQ number.
     */
    auto PrivateChat::notifySession(std::int64_t user_id) -> void {
        Json::Value event{ Json::objectValue };
        event["type"] = "session_updated";
        event["user_id"] = std::to_string(user_id);
        admin::publish_event(std::move(event));
    }

    /**
     * @brief Persist one message and broadcast it only after a successful append.
     * @param peer Friend identity.
     * @param direction Incoming or outgoing.
     * @param content Message text.
     * @param status Received, ignored, sent, or failed.
     * @param error Ignore or delivery failure reason.
     * @return Task completing after the append attempt; storage errors are logged.
     */
    auto PrivateChat::appendMessage(
        Peer peer,
        std::string direction,
        std::string content,
        std::string status,
        std::string error
    ) -> drogon::Task<> {
        try {
            auto saved{ co_await storage::append_message(m_database, {
                                                                         .timestamp = common::unix_seconds(),
                                                                         .user_id = peer.user_id,
                                                                         .nickname = std::move(peer.nickname),
                                                                         .direction = std::move(direction),
                                                                         .content = std::move(content),
                                                                         .status = std::move(status),
                                                                         .error = std::move(error),
                                                                     }) };
            Json::Value event{ Json::objectValue };
            event["type"] = "message_created";
            event["user_id"] = std::to_string(peer.user_id);
            event["message"] = std::move(saved);
            admin::publish_event(std::move(event));
        } catch (std::exception const& failure) {
            LOG_ERROR << "Recording private message failed: " << failure.what();
        }
    }

} // namespace workflow
