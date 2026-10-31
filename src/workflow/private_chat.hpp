/**
 * @file private_chat.hpp
 * @brief Declare the workflow that answers private messages with the model.
 */
#pragma once

#include <cstdint>
#include <memory>
#include <string>

#include <drogon/orm/DbClient.h>
#include <drogon/utils/coroutine.h>

#include <drogon_onebot/onebot_client.hpp>
#include <drogon_openai/openai_client.hpp>

#include "workflow/bot_settings.hpp"
#include "workflow/session_store.hpp"

namespace workflow {

    class AgentRuntime;

    /**
     * @brief Answer when the model gives no reply.
     */
    constexpr char const* FAILURE_REPLY{ "抱歉，暂时无法回复，请稍后再试。" };

    /**
     * @brief Friend that a reply goes to, with what the log shows of them.
     */
    struct Peer {
        std::int64_t self_id{};              ///< QQ number of the bot account.
        std::int64_t user_id{};              ///< QQ number of the friend.
        std::string nickname;                ///< Nickname of the friend; may be empty.
        std::int64_t group_id{};             ///< Group recipient; zero denotes a private conversation.
        std::string additional_prompt;       ///< Per-group instructions copied when an event is accepted.
        std::int32_t message_id{};           ///< Current incoming message available for quoting.
        std::vector<std::string> image_urls; ///< Bounded incoming HTTP image locations.
    };

    /**
     * @brief Map private and group recipients to disjoint context and storage keys.
     * @param peer Message sender and optional group.
     * @return Positive friend QQ or negative group QQ.
     */
    auto conversation_id(Peer const& peer) -> std::int64_t;

    /**
     * @brief Parse an administrative conversation key without accepting zero or INT64_MIN.
     * @param value Positive friend QQ or negative group QQ as decimal text.
     * @return Validated storage key.
     */
    auto parse_conversation(std::string const& value) -> std::int64_t;

    /**
     * @brief Answer the private messages of friends with the model.
     *
     * Asks the model with each friend's recorded history, and sends the replies. The
     * plugin decides which messages reach it, and shares one instance with its message
     * handler through a `std::shared_ptr`, so the state lives as long as any handler that runs.
     *
     * Each reply reads its request settings once. The session store reads live history
     * and idle limits on each operation; persistent messages never restore model context.
     */
    class PrivateChat {
    private:
        std::shared_ptr<BotSettings const> m_settings; ///< Settings in effect, read at the start of each reply.
        drogon_onebot::OneBot m_onebot;                ///< Client that sends the replies.
        drogon_openai::OpenAI m_openai;                ///< Client that asks the model.
        SessionStore m_sessions;                       ///< Conversation state of each user.
        drogon::orm::DbClientPtr m_database;           ///< Database for completed model usage.
        std::shared_ptr<AgentRuntime> m_agent;         ///< Optional role and tool execution runtime.
        std::string m_model;                           ///< Configured model name for failed requests.

    public:
        /**
         * @brief Bind the workflow to its settings and clients.
         * @param settings Settings of the bot, shared with whoever updates them.
         * @param onebot Client for the OneBot implementation's HTTP API.
         * @param openai Client for the model's API.
         * @param database Database for completed model usage.
         * @param model Configured model name for failed requests.
         */
        PrivateChat(
            std::shared_ptr<BotSettings const> settings,
            drogon_onebot::OneBot onebot,
            drogon_openai::OpenAI openai,
            drogon::orm::DbClientPtr database,
            std::string model
        );

        /**
         * @brief Answer a text message of a friend with the model.
         *
         * While the friend waits for a reply, their further messages are queued, and the call
         * that is answering sends them to the model together afterwards.
         * @param peer Friend who sent the message.
         * @param text Message text; not empty.
         * @return Task that ends when every reply that this call is responsible for has been sent.
         */
        auto Chat(Peer peer, std::string text) -> drogon::Task<>;

        /**
         * @brief Forget a friend's history and queued messages.
         *
         * A reply that is being generated is still sent, but not recorded.
         * @param user_id QQ number of the friend.
         */
        auto Reset(std::int64_t user_id) -> void;

        /**
         * @brief Append an incoming friend message, including ignored messages.
         * @param peer Sender identity.
         * @param content Text and readable media placeholders.
         * @param ignored_reason Empty for an accepted message, otherwise its exclusion reason.
         * @return Task that completes after the append attempt.
         */
        auto Observe(Peer peer, std::string content, std::string ignored_reason) -> drogon::Task<>;

        /**
         * @brief Copy metadata of in-memory sessions.
         * @return Independent session summaries.
         */
        [[nodiscard]] auto Sessions() const -> std::vector<SessionSummary>;

        /**
         * @brief Copy one in-memory conversation without creating it.
         * @param user_id Friend QQ number.
         * @return Session snapshot or empty.
         */
        [[nodiscard]] auto Session(std::int64_t user_id) const -> std::optional<SessionSnapshot>;

        /**
         * @brief Count the users whose session state has been created.
         * @return Number of sessions under the store lock.
         */
        [[nodiscard]] auto SessionCount() const -> std::size_t;

        /**
         * @brief Send a text message to a friend, and log it at INFO, or the failure at ERROR.
         * @param peer Recipient.
         * @param text Text to send.
         * @return Task that ends when the OneBot implementation has answered.
         */
        auto Send(Peer peer, std::string text) -> drogon::Task<>;

        /**
         * @brief Install optional execution services before message listeners start.
         * @param agent Shared runtime without a strong reference back to this workflow.
         */
        auto SetAgent(std::shared_ptr<AgentRuntime> agent) -> void;

        /**
         * @brief Send typed segments and persist the delivery outcome.
         * @param peer Current recipient.
         * @param message Typed message segments.
         * @param display Readable message text for history and logs.
         * @return Delivery status and optional message ID.
         */
        auto SendMessage(
            Peer peer,
            drogon_onebot::Message message,
            std::string display
        ) -> drogon::Task<Json::Value>;

        /**
         * @brief Describe current incoming images when the vision role is enabled.
         * @param peer Sender and incoming image URLs.
         * @param question Accompanying user text.
         * @return Image description or empty.
         */
        auto DescribeImages(Peer peer, std::string question) -> drogon::Task<std::string>;

    private:
        /**
         * @brief Ask the model about a message, then record and send the reply.
         *
         * Sends `FAILURE_REPLY` instead, and adds no context turn, when the model gives no reply.
         * @param peer Friend who asked.
         * @param prompt User message, possibly several queued ones joined by line breaks.
         * @return Task that ends when the reply has been sent.
         */
        auto answer(Peer peer, std::string prompt) -> drogon::Task<>;

        /**
         * @brief Persist one message and broadcast it only after a successful append.
         * @param peer Friend identity.
         * @param direction Incoming or outgoing.
         * @param content Message text.
         * @param status Received, ignored, sent, or failed.
         * @param error Ignore or delivery failure reason.
         * @return Task completing after the append attempt; storage errors are logged.
         */
        auto appendMessage(
            Peer peer,
            std::string direction,
            std::string content,
            std::string status,
            std::string error
        ) -> drogon::Task<>;

        /**
         * @brief Notify session subscribers that metadata or context changed.
         * @param user_id Friend QQ number.
         */
        static auto notifySession(std::int64_t user_id) -> void;
    };

} // namespace workflow
