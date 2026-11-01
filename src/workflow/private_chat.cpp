/**
 * @file private_chat.cpp
 * @brief Implement the workflow that answers private messages with the model.
 */
#include "workflow/private_chat.hpp"

#include <charconv>
#include <optional>
#include <utility>

#include <drogon/drogon.h>

#include "common/text.hpp"
#include "common/time.hpp"
#include "servers/live_events.hpp"
#include "storage/messages.hpp"
#include "storage/usage.hpp"
#include "workflow/access_control.hpp"
#include "workflow/agent.hpp"
#include "workflow/image_source.hpp"

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

        /**
         * @brief One received image's event fields.
         */
        struct ImageSource {
            std::string url;  ///< `url` field, for example an HTTP URL or `base64://` data.
            std::string file; ///< `file` field that identifies the image to the OneBot implementation.
        };

        /**
         * @brief Collect image locations from one message, bounded per reply.
         * @param message Message segments.
         * @param sources Output list; existing entries are kept.
         */
        auto collect_image_sources(drogon_onebot::Message const& message, std::vector<ImageSource>& sources) -> void {
            for (auto const& segment : message) {
                if (segment.type != "image" || sources.size() >= MAX_MESSAGE_IMAGES * 2) {
                    continue;
                }
                ImageSource source{};
                if (segment.data["url"].isString()) {
                    source.url = segment.data["url"].asString();
                }
                if (segment.data["file"].isString()) {
                    source.file = segment.data["file"].asString();
                }
                if (!source.url.empty() || !source.file.empty()) {
                    sources.push_back(std::move(source));
                }
            }
        }

        /**
         * @brief Read the ID of the message that this message quotes.
         * @param message Message segments.
         * @return Positive message ID, or empty when the message quotes nothing.
         */
        auto quoted_message_id(drogon_onebot::Message const& message) -> std::optional<std::int32_t> {
            for (auto const& segment : message) {
                if (segment.type != "reply" || !segment.data["id"].isString()) {
                    continue;
                }
                auto const text{ segment.data["id"].asString() };
                std::int32_t id{};
                auto const [end, error]{ std::from_chars(text.data(), text.data() + text.size(), id) };
                if (error == std::errc{} && end == text.data() + text.size() && id > 0) {
                    return id;
                }
            }
            return std::nullopt;
        }

        /**
         * @brief Turn one received image into a vision-ready location.
         *
         * HTTP and `base64://` locations are handled directly. Anything else is fetched with
         * `get_image`, preferring the returned HTTP URL, then its base64 content, then a local
         * file on this host.
         * @param onebot OneBot client.
         * @param source Image event fields.
         * @return Vision-ready location, or empty when the source cannot be read.
         */
        auto resolve_image_source(drogon_onebot::OneBot onebot, ImageSource source) -> drogon::Task<std::optional<std::string>> {
            if (vision_image_url(source.url)) {
                co_return source.url;
            }
            if (source.url.starts_with("base64://")) {
                if (auto data{ image_data_url(std::string_view{ source.url }.substr(9)) }) {
                    co_return std::move(*data);
                }
            }
            if (vision_image_url(source.file)) {
                co_return source.file;
            }
            if (source.file.starts_with("base64://")) {
                if (auto data{ image_data_url(std::string_view{ source.file }.substr(9)) }) {
                    co_return std::move(*data);
                }
            }
            if (auto data{ image_file_url(source.url) }) {
                co_return std::move(*data);
            }
            if (auto data{ image_file_url(source.file) }) {
                co_return std::move(*data);
            }
            auto const file{ !source.file.empty() ? source.file : source.url };
            if (file.empty()) {
                co_return std::nullopt;
            }
            auto const fetched{ co_await onebot.GetImage({ .file = file, .timeout = 10.0 }) };
            if (!fetched) {
                LOG_WARN << "Fetching image " << file << " failed: " << fetched.error().message;
                co_return std::nullopt;
            }
            if (fetched->raw["url"].isString()) {
                auto const& url{ fetched->raw["url"].asString() };
                if (vision_image_url(url)) {
                    co_return url;
                }
                if (url.starts_with("base64://")) {
                    if (auto data{ image_data_url(std::string_view{ url }.substr(9)) }) {
                        co_return std::move(*data);
                    }
                }
            }
            if (fetched->raw["base64"].isString()) {
                auto text{ fetched->raw["base64"].asString() };
                if (text.starts_with("base64://")) {
                    text.erase(0, 9);
                }
                if (auto data{ image_data_url(text) }) {
                    co_return std::move(*data);
                }
            }
            if (auto data{ image_file_url(fetched->file) }) {
                co_return std::move(*data);
            }
            co_return std::nullopt;
        }

    } // namespace

    /**
     * @brief Map private and group recipients to disjoint context and storage keys.
     * @param peer Message sender and optional group.
     * @return Positive friend QQ or negative group QQ.
     */
    auto conversation_id(Peer const& peer) -> std::int64_t {
        return peer.group_id == 0 ? peer.user_id : -peer.group_id;
    }

    /**
     * @brief Parse an administrative conversation key without accepting zero or INT64_MIN.
     * @param value Positive friend QQ or negative group QQ as decimal text.
     * @return Validated storage key.
     */
    auto parse_conversation(std::string const& value) -> std::int64_t {
        return value.starts_with('-') ? -parse_qq(value.substr(1)) : parse_qq(value);
    }

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
    ) : m_settings{ std::move(settings) },
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
        auto const claimed{ m_sessions.Enqueue(conversation_id(peer), std::move(text), SessionStore::Clock::now()) };
        notifySession(conversation_id(peer));
        if (!claimed) {
            co_return;
        }
        try {
            while (auto prompt{ m_sessions.TakePending(conversation_id(peer)) }) {
                co_await answer(peer, std::move(*prompt));
            }
            notifySession(conversation_id(peer));
        } catch (...) {
            // A claim left behind would queue this user's messages forever.
            m_sessions.Release(conversation_id(peer));
            notifySession(conversation_id(peer));
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
        auto const history{ m_sessions.History(conversation_id(peer)) };
        drogon_openai::ChatCompletionCreateParams params{};
        if (!config->system_prompt.empty()) {
            params.messages.push_back({ .role = "system", .content = config->system_prompt });
        }
        for (auto const& turn : history.turns) {
            params.messages.push_back({ .role = "user", .content = turn.user });
            params.messages.push_back({ .role = "assistant", .content = turn.assistant });
        }
        if (!peer.additional_prompt.empty()) {
            params.messages.push_back({ .role = "system", .content = peer.additional_prompt });
        }
        params.messages.push_back({ .role = "user", .content = prompt });
        if (!config->model.empty()) {
            params.model = config->model;
        }
        params.max_tokens = config->max_tokens;
        params.timeout = config->request_timeout;

        if (m_agent) {
            auto result{ co_await m_agent->Run(
                peer,
                std::move(params),
                prompt,
                [this, peer, maximum = config->max_reply_chars](
                    drogon_onebot::Message message,
                    std::string display
                ) -> drogon::Task<Json::Value> {
                    std::string text{};
                    bool text_only{ true };
                    drogon_onebot::Message quotes{};
                    for (auto const& segment : message) {
                        if (segment.type == "text") {
                            text += segment.data["text"].asString();
                        } else if (segment.type == "reply") {
                            quotes.push_back(segment);
                        } else {
                            text_only = false;
                        }
                    }
                    if (text_only && !text.empty() && text == display) {
                        Json::Value result{};
                        for (auto& part : common::split_reply(text, maximum)) {
                            auto segments{ std::move(quotes) };
                            quotes.clear();
                            segments.push_back(drogon_onebot::MessageSegment::Text(part));
                            result = co_await SendMessage(peer, std::move(segments), part);
                            if (!result["ok"].asBool()) {
                                co_return result;
                            }
                        }
                        co_return result;
                    }
                    co_return co_await SendMessage(peer, std::move(message), std::move(display));
                }
            ) };
            if (!result.completed) {
                co_await Send(peer, FAILURE_REPLY);
                co_return;
            }
            if (!result.text.empty()) {
                m_sessions.Record(
                    conversation_id(peer),
                    history.generation,
                    { .user = prompt, .assistant = result.text },
                    SessionStore::Clock::now()
                );
                if (!result.delivered) {
                    for (auto& part : common::split_reply(result.text, config->max_reply_chars)) {
                        co_await Send(peer, std::move(part));
                    }
                }
            }
            co_return;
        }
        auto const completion{ co_await m_openai.chat.completions.Create(std::move(params)) };
        auto reply{ reply_text(completion, peer.user_id) };
        storage::UsageRecord usage{
            .timestamp = std::chrono::duration_cast<std::chrono::seconds>(std::chrono::system_clock::now().time_since_epoch()).count(),
            .user_id = conversation_id(peer),
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
            event["user_id"] = std::to_string(conversation_id(peer));
            admin::publish_event(std::move(event));
        } catch (std::exception const& error) {
            LOG_ERROR << "Recording model usage failed: " << error.what();
        }
        if (reply.empty()) {
            co_await Send(peer, FAILURE_REPLY);
            co_return;
        }
        auto parts{ common::split_reply(reply, config->max_reply_chars) };
        m_sessions.Record(
            conversation_id(peer),
            history.generation,
            { .user = std::move(prompt), .assistant = std::move(reply) },
            SessionStore::Clock::now()
        );
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
        static_cast<void>(co_await SendMessage(
            peer,
            { drogon_onebot::MessageSegment::Text(text) },
            text
        ));
    }

    /**
     * @brief Install optional execution services before message listeners start.
     * @param agent Shared runtime without a strong reference back to this workflow.
     */
    auto PrivateChat::SetAgent(std::shared_ptr<AgentRuntime> agent) -> void {
        m_agent = std::move(agent);
    }

    /**
     * @brief Resolve accepted images, including those quoted by a reply, for the vision role.
     *
     * HTTP and `base64://` sources are kept or converted directly; other sources are
     * fetched through the OneBot implementation. At most four images are returned, and
     * a source that fails is skipped.
     * @param peer Sender and recipient, used to restrict quoted messages to this chat.
     * @param message Accepted incoming message.
     * @return Vision-ready HTTP or data URLs.
     */
    auto PrivateChat::ResolveImages(Peer peer, drogon_onebot::Message message) -> drogon::Task<std::vector<std::string>> {
        if (!m_agent || !m_agent->VisionEnabled()) {
            co_return std::vector<std::string>{};
        }
        std::vector<ImageSource> sources{};
        collect_image_sources(message, sources);
        if (auto const quoted{ quoted_message_id(message) }; quoted && sources.size() < MAX_MESSAGE_IMAGES) {
            auto const fetched{ co_await m_onebot.GetMsg({ .message_id = *quoted, .timeout = 10.0 }) };
            if (!fetched) {
                LOG_WARN << "Fetching quoted message " << *quoted << " failed: " << fetched.error().message;
            } else {
                bool const same_chat{
                    peer.group_id == 0 ?
                        fetched->message_type == "private" :
                        fetched->group_id && *fetched->group_id == peer.group_id
                };
                if (same_chat) {
                    collect_image_sources(fetched->message, sources);
                }
            }
        }
        std::vector<std::string> urls{};
        std::size_t total{};
        for (auto const& source : sources) {
            if (urls.size() >= MAX_MESSAGE_IMAGES || total >= MAX_MESSAGE_IMAGE_DATA_BYTES) {
                break;
            }
            auto url{ co_await resolve_image_source(m_onebot, std::move(source)) };
            if (!url || url->size() > MAX_MESSAGE_IMAGE_DATA_BYTES - total) {
                continue;
            }
            total += url->size();
            urls.push_back(std::move(*url));
        }
        co_return urls;
    }

    /**
     * @brief Describe current incoming images when the vision role is enabled.
     * @param peer Sender and incoming image URLs.
     * @param question Accompanying user text.
     * @return Image description or empty.
     */
    auto PrivateChat::DescribeImages(
        Peer peer,
        std::string question
    ) -> drogon::Task<std::string> {
        if (!m_agent) {
            co_return std::string{};
        }
        auto urls{ peer.image_urls };
        co_return co_await m_agent->DescribeImages(
            std::move(peer),
            std::move(urls),
            std::move(question)
        );
    }

    /**
     * @brief Send typed segments and persist the delivery outcome.
     * @param peer Current recipient.
     * @param message Typed message segments.
     * @param display Readable message text for history and logs.
     * @return Delivery status and optional message ID.
     */
    auto PrivateChat::SendMessage(
        Peer peer,
        drogon_onebot::Message message,
        std::string display
    ) -> drogon::Task<Json::Value> {
        auto const sent{
            peer.group_id == 0 ?
                co_await m_onebot.SendPrivateMsg({ .user_id = peer.user_id, .message = std::move(message) }) :
                co_await m_onebot.SendGroupMsg({ .group_id = peer.group_id, .message = std::move(message) })
        };
        Json::Value result{};
        result["ok"] = sent.has_value();
        if (!sent) {
            result["error"] = sent.error().message;
            LOG_ERROR << "Sending a message to " << peer.user_id << " failed: " << sent.error().message;
            co_await appendMessage(peer, "outgoing", std::move(display), "failed", sent.error().message);
        } else {
            result["message_id"] = std::to_string(sent->message_id);
            LOG_INFO << common::describe_sent(peer.self_id, peer.user_id, peer.nickname);
            co_await appendMessage(peer, "outgoing", std::move(display), "sent", {});
        }
        co_return result;
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
            m_sessions.Touch(conversation_id(peer), peer.nickname, common::unix_seconds());
        }
        co_await appendMessage(
            peer,
            "incoming",
            std::move(content),
            ignored_reason.empty() ? "received" : "ignored",
            ignored_reason
        );
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
            auto saved{
                co_await storage::append_message(
                    m_database,
                    {
                        .timestamp = common::unix_seconds(),
                        .user_id = conversation_id(peer),
                        .nickname = std::move(peer.nickname),
                        .direction = std::move(direction),
                        .content = std::move(content),
                        .status = std::move(status),
                        .error = std::move(error),
                        .sender_id = peer.user_id,
                        .group_id = peer.group_id,
                    }
                )
            };
            Json::Value event{ Json::objectValue };
            event["type"] = "message_created";
            event["user_id"] = std::to_string(conversation_id(peer));
            event["message"] = std::move(saved);
            admin::publish_event(std::move(event));
        } catch (std::exception const& failure) {
            LOG_ERROR << "Recording private message failed: " << failure.what();
        }
    }

} // namespace workflow
