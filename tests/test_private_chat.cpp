/**
 * @file test_private_chat.cpp
 * @brief Check the bot end to end: signed events in, model requests and QQ messages out.
 *
 * The app serves a fake NapCat HTTP API under `/napcat` and a fake OpenAI-format API under
 * `/v1`, which `tests/config.json` points the two plugin libraries at. The fake model echoes
 * the last user message, except for a few markers that make it fail, wait, or answer with
 * Markdown or a long text. Each test uses its own QQ number, so the histories stay apart.
 */
#include <algorithm>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <deque>
#include <future>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <thread>
#include <utility>
#include <vector>

#include <drogon/drogon.h>
#include <drogon/drogon_test.h>
#include <drogon/utils/coroutine.h>

#include "controllers/onebot/assistant_bot.hpp"
#include "storage/database.hpp"
#include "storage/messages.hpp"
#include "storage/usage.hpp"
#include "support.hpp"
#include "workflow/bot_config.hpp"
#include "workflow/private_chat.hpp"

namespace {

    constexpr std::int64_t SELF_ID{ 10001 };                            ///< QQ number of the bot account.
    constexpr auto REPLY_WAIT{ std::chrono::milliseconds{ 3000 } };     ///< Longest wait for a message that the bot should send.
    constexpr auto NO_REPLY_WAIT{ std::chrono::milliseconds{ 300 } };   ///< Wait after which an unsent message counts as not sent.
    constexpr char const* SYSTEM_PROMPT{ "You are a test assistant." }; ///< `system_prompt` in tests/config.json.
    constexpr double SLOW_DELAY{ 1.0 };                                 ///< Seconds that the fake model takes for `slow`.
    constexpr std::size_t LONG_REPLY_CHARS{ 250 };                      ///< Length of the fake model's answer to `long`.

    /**
     * @brief Texts that the fake NapCat API was asked to send, by recipient.
     */
    class SentMessages {
    private:
        std::mutex m_mutex;                                      ///< Guards `m_texts`.
        std::condition_variable m_ready;                         ///< Signaled after each `Push()`.
        std::map<std::int64_t, std::deque<std::string>> m_texts; ///< Texts not yet taken, oldest first.
        std::size_t m_total{};                                   ///< Total attempts observed, including failed deliveries.

    public:
        /**
         * @brief Record a sent text and wake a waiting `Pop()`.
         * @param user_id QQ number of the recipient.
         * @param text Text of the message.
         */
        auto Push(std::int64_t user_id, std::string text) -> void {
            {
                std::lock_guard lock{ m_mutex };
                m_texts[user_id].push_back(std::move(text));
                ++m_total;
            }
            m_ready.notify_all();
        }

        /**
         * @brief Take the oldest text sent to a user, waiting for one if there is none.
         * @param user_id QQ number of the recipient.
         * @param timeout Longest wait.
         * @return The text, or empty if none arrived in time.
         */
        [[nodiscard]] auto Pop(
            std::int64_t user_id,
            std::chrono::milliseconds timeout
        ) -> std::optional<std::string> {
            std::unique_lock lock{ m_mutex };
            auto& texts{ m_texts[user_id] };
            if (!m_ready.wait_for(lock, timeout, [&texts] { return !texts.empty(); })) {
                return std::nullopt;
            }
            auto text{ std::move(texts.front()) };
            texts.pop_front();
            return text;
        }

        /**
         * @brief Count every delivery attempt observed by the fake server.
         * @return Total received requests, including messages already popped.
         */
        [[nodiscard]] auto Count() -> std::size_t {
            std::lock_guard lock{ m_mutex };
            return m_total;
        }
    };

    /**
     * @brief `messages` arrays that the fake model received.
     */
    class ModelRequests {
    private:
        std::mutex m_mutex;                          ///< Guards `m_requests`.
        std::vector<Json::Value> m_requests;         ///< Every `messages` array, oldest first.
        std::map<std::string, Json::Value> m_params; ///< Latest complete request by prompt.

    public:
        /**
         * @brief Record one model request and its message array.
         * @param request The complete request body.
         */
        auto Push(Json::Value request) -> void {
            std::lock_guard lock{ m_mutex };
            auto const messages{ request["messages"] };
            m_params[messages[messages.size() - 1]["content"].asString()] = std::move(request);
            m_requests.push_back(messages);
        }

        /**
         * @brief Find complete request parameters by prompt.
         * @param prompt Last message's content.
         * @return Captured JSON request, or empty when absent.
         */
        auto Params(std::string const& prompt) -> std::optional<Json::Value> {
            std::lock_guard lock{ m_mutex };
            auto const found{ m_params.find(prompt) };
            return found == m_params.end() ? std::nullopt : std::optional{ found->second };
        }

        /**
         * @brief Find the latest request whose last message has the given content.
         * @param content `content` of the request's last message.
         * @return Its `messages` array, or empty if no request matches.
         */
        [[nodiscard]] auto Find(std::string_view content) -> std::optional<Json::Value> {
            std::lock_guard lock{ m_mutex };
            for (auto it{ m_requests.rbegin() }; it != m_requests.rend(); ++it) {
                if (!it->empty() && (*it)[it->size() - 1]["content"].asString() == content) {
                    return *it;
                }
            }
            return std::nullopt;
        }
    };

    SentMessages g_sent{};      ///< Filled by the fake NapCat API on the IO thread.
    ModelRequests g_requests{}; ///< Filled by the fake model on the IO thread.

    /**
     * @brief Build an OpenAI-format completion.
     * @param content Reply text.
     * @return Completion object with one choice.
     */
    auto completion_json(std::string const& content) -> Json::Value {
        Json::Value completion{ Json::objectValue };
        completion["id"] = "chatcmpl-test";
        completion["object"] = "chat.completion";
        completion["created"] = 1700000000;
        completion["model"] = "fake-model";
        Json::Value choice{ Json::objectValue };
        choice["index"] = 0;
        choice["finish_reason"] = "stop";
        choice["message"]["role"] = "assistant";
        choice["message"]["content"] = content;
        completion["choices"].append(choice);
        completion["usage"]["prompt_tokens"] = 5;
        completion["usage"]["completion_tokens"] = 3;
        completion["usage"]["total_tokens"] = 8;
        return completion;
    }

    /**
     * @brief Register the fake NapCat HTTP API and the fake model on the app.
     */
    auto serve_fakes() -> void {
        drogon::app().registerHandler(
            "/napcat/send_private_msg",
            [](drogon::HttpRequestPtr req) -> drogon::Task<drogon::HttpResponsePtr> {
                auto const body{ req->getJsonObject() };
                std::string text{};
                for (auto const& segment : (*body)["message"]) {
                    text += segment["data"]["text"].asString();
                }
                g_sent.Push((*body)["user_id"].asInt64(), std::move(text));
                if ((*body)["user_id"].asInt64() == 20102) {
                    co_return drogon::HttpResponse::newHttpResponse(drogon::k503ServiceUnavailable, drogon::CT_TEXT_PLAIN);
                }

                Json::Value reply{ Json::objectValue };
                reply["status"] = "ok";
                reply["retcode"] = 0;
                reply["data"]["message_id"] = 1;
                co_return drogon::HttpResponse::newHttpJsonResponse(reply);
            },
            { drogon::Post }
        );
        drogon::app().registerHandler(
            "/v1/chat/completions",
            [](drogon::HttpRequestPtr req) -> drogon::Task<drogon::HttpResponsePtr> {
                auto const messages{ (*req->getJsonObject())["messages"] };
                g_requests.Push(*req->getJsonObject());
                auto const prompt{ messages[messages.size() - 1]["content"].asString() };

                if (prompt == "fail") {
                    auto response{ drogon::HttpResponse::newHttpResponse(drogon::k503ServiceUnavailable, drogon::CT_TEXT_PLAIN) };
                    response->setBody("overloaded");
                    co_return response;
                }
                if (prompt.starts_with("slow")) {
                    co_await drogon::sleepCoro(trantor::EventLoop::getEventLoopOfCurrentThread(), SLOW_DELAY);
                }
                std::string reply{ "echo: " + prompt };
                if (prompt == "markdown") {
                    reply = "## Title\n**bold** and `code`";
                } else if (prompt == "long") {
                    reply = std::string(LONG_REPLY_CHARS, 'x');
                }
                auto completion{ completion_json(reply) };
                completion["model"] = (*req->getJsonObject())["model"];
                if (prompt == "missing usage") {
                    completion.removeMember("usage");
                } else if (prompt == "typed cache" || prompt == "zero cache") {
                    completion["usage"]["prompt_tokens_details"]["cached_tokens"] = prompt == "zero cache" ? 0 : 3;
                    completion["usage"]["prompt_cache_hit_tokens"] = 4;
                } else if (prompt == "deepseek cache") {
                    completion["usage"]["prompt_cache_hit_tokens"] = 4;
                }
                co_return drogon::HttpResponse::newHttpJsonResponse(completion);
            },
            { drogon::Post }
        );
    }

    /**
     * @brief Build a message segment array with one `text` segment.
     * @param text Text of the segment.
     * @return Message in array format.
     */
    auto text_message(std::string const& text) -> Json::Value {
        Json::Value segment{ Json::objectValue };
        segment["type"] = "text";
        segment["data"]["text"] = text;
        Json::Value message{ Json::arrayValue };
        message.append(segment);
        return message;
    }

    /**
     * @brief Build a message event as NapCat posts it.
     * @param user_id QQ number of the sender.
     * @param message Message in array format.
     * @param message_type `private` or `group`.
     * @param sub_type `friend` or `group` for private messages, `normal` for group ones.
     * @return Event object.
     */
    auto message_event(
        std::int64_t user_id,
        Json::Value const& message,
        char const* message_type = "private",
        char const* sub_type = "friend"
    ) -> Json::Value {
        Json::Value event{ Json::objectValue };
        event["time"] = 1700000000;
        event["self_id"] = SELF_ID;
        event["post_type"] = "message";
        event["message_type"] = message_type;
        event["sub_type"] = sub_type;
        event["message_id"] = 1;
        event["user_id"] = user_id;
        if (std::string_view{ message_type } == "group") {
            event["group_id"] = 40001;
        }
        event["message"] = message;
        event["raw_message"] = "";
        event["font"] = 14;
        event["sender"]["user_id"] = user_id;
        event["sender"]["nickname"] = "tester";
        return event;
    }

    /**
     * @brief Post a private text message from a friend.
     * @param user_id QQ number of the sender.
     * @param text Message text.
     * @return HTTP status of the event route's answer.
     */
    auto say(std::int64_t user_id, std::string const& text) -> drogon::HttpStatusCode {
        return test_support::post_event(message_event(user_id, text_message(text)))->statusCode();
    }

    /**
     * @brief Send an authenticated administrative JSON request.
     * @param path Request path.
     * @param method HTTP method.
     * @param body JSON body; null sends no body.
     * @return HTTP response.
     */
    auto admin_request(
        char const* path,
        drogon::HttpMethod method,
        Json::Value const& body = Json::Value{}
    ) -> drogon::HttpResponsePtr {
        auto request{ body.isNull() ? drogon::HttpRequest::newHttpRequest() : drogon::HttpRequest::newHttpJsonRequest(body) };
        request->setMethod(method);
        request->setPath(path);
        request->addCookie("assistantbot_admin_session", test_support::login_admin());
        auto const client{ drogon::HttpClient::newHttpClient(test_support::SERVER_URL) };
        auto const [result, response]{ client->sendRequest(request, 5.0) };
        if (result != drogon::ReqResult::Ok) {
            throw std::runtime_error{ "administrative request failed" };
        }
        return response;
    }

    /**
     * @brief List the roles and contents of a `messages` array.
     * @param messages Request messages.
     * @return One `role: content` entry per message, joined by ` | `.
     */
    auto describe(Json::Value const& messages) -> std::string {
        std::string result{};
        for (auto const& message : messages) {
            if (!result.empty()) {
                result += " | ";
            }
            result += message["role"].asString() + ": " + message["content"].asString();
        }
        return result;
    }

    /**
     * @brief Wait for asynchronous message persistence after the fake delivery response.
     * @param user_id Friend identifier.
     * @param count Expected minimum row count.
     * @return Latest chronological message page.
     * @throws std::runtime_error If the rows do not arrive in time.
     */
    auto wait_messages(std::int64_t user_id, std::size_t count) -> Json::Value {
        auto const db{ drogon::app().getPlugin<storage::Database>()->Client() };
        auto const deadline{ std::chrono::steady_clock::now() + REPLY_WAIT };
        while (std::chrono::steady_clock::now() < deadline) {
            auto page{ drogon::sync_wait(storage::message_page(db, user_id, 200)) };
            if (page["messages"].size() >= count) {
                return page;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds{ 10 });
        }
        throw std::runtime_error{ "message persistence timed out" };
    }

    /**
     * @brief Finish chat persistence and claims before the fixture destroys its I/O loops.
     *
     * A fake NapCat request wakes Pop before the real sender receives its response or
     * appends the result. Closing the app there lets a database callback outlive its
     * caller loop. Check persisted attempts and idle sessions, then pass each loop's
     * already-queued continuations before allowing shutdown.
     * @throws std::runtime_error If an accepted workflow does not finish in time.
     */
    auto drain_chat() -> void {
        auto const db{ drogon::app().getPlugin<storage::Database>()->Client() };
        auto const chat{ drogon::app().getPlugin<controllers::AssistantBot>()->Chat() };
        auto const deadline{ std::chrono::steady_clock::now() + REPLY_WAIT };
        while (std::chrono::steady_clock::now() < deadline) {
            auto const rows{ db->execSqlSync("SELECT count(*) AS n FROM chat_messages WHERE direction = 'outgoing'") };
            auto const sessions{ chat->Sessions() };
            if (rows.at(0)["n"].as<std::size_t>() >= g_sent.Count() && std::ranges::none_of(sessions, [](auto const& session) { return session.busy; })) {
                for (std::size_t i{}; i < drogon::app().getThreadNum(); ++i) {
                    auto barrier{ std::make_shared<std::promise<void>>() };
                    auto ready{ barrier->get_future() };
                    drogon::app().getIOLoop(i)->queueInLoop([barrier] { barrier->set_value(); });
                    ready.get();
                }
                return;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds{ 10 });
        }
        throw std::runtime_error{ "chat workflows did not finish before shutdown" };
    }

} // namespace

DROGON_TEST(RepliesToFriend) {
    CHECK(say(20001, "hello") == drogon::k204NoContent);
    CHECK(g_sent.Pop(20001, REPLY_WAIT) == "echo: hello");

    auto const request{ g_requests.Find("hello") };
    REQUIRE(request.has_value());
    CHECK(describe(*request) == std::string{ "system: " } + SYSTEM_PROMPT + " | user: hello");
}

DROGON_TEST(SendsHistoryBack) {
    CHECK(say(20002, "one") == drogon::k204NoContent);
    CHECK(g_sent.Pop(20002, REPLY_WAIT) == "echo: one");
    CHECK(say(20002, "two") == drogon::k204NoContent);
    CHECK(g_sent.Pop(20002, REPLY_WAIT) == "echo: two");

    auto const request{ g_requests.Find("two") };
    REQUIRE(request.has_value());
    CHECK(describe(*request) == std::string{ "system: " } + SYSTEM_PROMPT + " | user: one | assistant: echo: one | user: two");
}

DROGON_TEST(KeepsOnlyTheLatestTurns) {
    // tests/config.json keeps two turns.
    for (auto const* text : { "t1", "t2", "t3", "t4" }) {
        CHECK(say(20003, text) == drogon::k204NoContent);
        CHECK(g_sent.Pop(20003, REPLY_WAIT) == std::string{ "echo: " } + text);
    }
    auto const request{ g_requests.Find("t4") };
    REQUIRE(request.has_value());
    CHECK(describe(*request) == std::string{ "system: " } + SYSTEM_PROMPT + " | user: t2 | assistant: echo: t2 | user: t3 | assistant: echo: t3 | user: t4");
}

DROGON_TEST(ResetClearsHistory) {
    CHECK(say(20004, "r1") == drogon::k204NoContent);
    CHECK(g_sent.Pop(20004, REPLY_WAIT) == "echo: r1");
    CHECK(say(20004, controllers::RESET_COMMAND) == drogon::k204NoContent);
    CHECK(g_sent.Pop(20004, REPLY_WAIT) == controllers::RESET_REPLY);
    CHECK(say(20004, "r2") == drogon::k204NoContent);
    CHECK(g_sent.Pop(20004, REPLY_WAIT) == "echo: r2");

    auto const request{ g_requests.Find("r2") };
    REQUIRE(request.has_value());
    CHECK(describe(*request) == std::string{ "system: " } + SYSTEM_PROMPT + " | user: r2");
}

DROGON_TEST(AnswersHelpWithoutTheModel) {
    CHECK(say(20005, controllers::HELP_COMMAND) == drogon::k204NoContent);
    CHECK(g_sent.Pop(20005, REPLY_WAIT) == controllers::HELP_REPLY);
    CHECK(!g_requests.Find(controllers::HELP_COMMAND).has_value());
}

DROGON_TEST(AnswersMessageWithoutText) {
    Json::Value image{ Json::objectValue };
    image["type"] = "image";
    image["data"]["file"] = "photo.png";
    Json::Value message{ Json::arrayValue };
    message.append(image);
    CHECK(test_support::post_event(message_event(20006, message))->statusCode() == drogon::k204NoContent);
    CHECK(g_sent.Pop(20006, REPLY_WAIT) == controllers::TEXT_ONLY_REPLY);
}

DROGON_TEST(ReportsModelFailureWithoutRecording) {
    CHECK(say(20007, "fail") == drogon::k204NoContent);
    CHECK(g_sent.Pop(20007, REPLY_WAIT) == workflow::FAILURE_REPLY);
    CHECK(say(20007, "after") == drogon::k204NoContent);
    CHECK(g_sent.Pop(20007, REPLY_WAIT) == "echo: after");

    auto const request{ g_requests.Find("after") };
    REQUIRE(request.has_value());
    CHECK(describe(*request) == std::string{ "system: " } + SYSTEM_PROMPT + " | user: after");
}

DROGON_TEST(MergesMessagesSentWhileBusy) {
    CHECK(say(20008, "slow") == drogon::k204NoContent);
    CHECK(say(20008, "a") == drogon::k204NoContent);
    CHECK(say(20008, "b") == drogon::k204NoContent);
    CHECK(g_sent.Pop(20008, REPLY_WAIT) == "echo: slow");
    CHECK(g_sent.Pop(20008, REPLY_WAIT) == "echo: a\nb");
    CHECK(!g_sent.Pop(20008, NO_REPLY_WAIT).has_value());
    CHECK(!g_requests.Find("a").has_value());
}

DROGON_TEST(StripsMarkdown) {
    CHECK(say(20009, "markdown") == drogon::k204NoContent);
    CHECK(g_sent.Pop(20009, REPLY_WAIT) == "Title\nbold and code");
}

DROGON_TEST(SplitsLongReplies) {
    // tests/config.json allows 100 characters per message.
    CHECK(say(20010, "long") == drogon::k204NoContent);
    CHECK(g_sent.Pop(20010, REPLY_WAIT) == std::string(100, 'x'));
    CHECK(g_sent.Pop(20010, REPLY_WAIT) == std::string(100, 'x'));
    CHECK(g_sent.Pop(20010, REPLY_WAIT) == std::string(50, 'x'));
}

DROGON_TEST(PauseDropsNewMessagesBeforeCommandsAndQueue) {
    Json::Value pause{ Json::objectValue };
    pause["paused"] = true;
    auto const paused{ admin_request("/api/admin/bot-status", drogon::Post, pause) };
    REQUIRE(paused->statusCode() == drogon::k200OK);
    CHECK((*paused->getJsonObject())["paused"].asBool());

    CHECK(say(20011, controllers::HELP_COMMAND) == drogon::k204NoContent);
    CHECK(say(20011, controllers::RESET_COMMAND) == drogon::k204NoContent);
    CHECK(say(20011, "paused prompt") == drogon::k204NoContent);
    CHECK(!g_sent.Pop(20011, NO_REPLY_WAIT).has_value());
    CHECK(!g_requests.Find("paused prompt").has_value());

    pause["paused"] = false;
    auto const resumed{ admin_request("/api/admin/bot-status", drogon::Post, pause) };
    REQUIRE(resumed->statusCode() == drogon::k200OK);
    CHECK(!(*resumed->getJsonObject())["paused"].asBool());
    CHECK(say(20011, "after pause") == drogon::k204NoContent);
    CHECK(g_sent.Pop(20011, REPLY_WAIT) == "echo: after pause");

    auto const request{ g_requests.Find("after pause") };
    REQUIRE(request.has_value());
    CHECK(describe(*request) == std::string{ "system: " } + SYSTEM_PROMPT + " | user: after pause");
}

DROGON_TEST(RecordsSuccessfulFailedAndUnreportedModelUsage) {
    auto const database{ drogon::app().getPlugin<storage::Database>()->Client() };
    auto const before{ drogon::sync_wait(storage::summarize_usage(database, 0, 4102444800)) };
    CHECK(say(20012, "usage test") == drogon::k204NoContent);
    CHECK(g_sent.Pop(20012, REPLY_WAIT) == "echo: usage test");
    CHECK(say(20012, "fail") == drogon::k204NoContent);
    CHECK(g_sent.Pop(20012, REPLY_WAIT) == workflow::FAILURE_REPLY);
    CHECK(say(20012, "missing usage") == drogon::k204NoContent);
    CHECK(g_sent.Pop(20012, REPLY_WAIT) == "echo: missing usage");
    auto const after{ drogon::sync_wait(storage::summarize_usage(database, 0, 4102444800)) };
    CHECK(after["requests"].asInt64() - before["requests"].asInt64() == 3);
    CHECK(after["successful_requests"].asInt64() - before["successful_requests"].asInt64() == 2);
    CHECK(after["reported_requests"].asInt64() - before["reported_requests"].asInt64() == 1);
    CHECK(after["total_tokens"].asInt64() - before["total_tokens"].asInt64() == 8);
    CHECK(drogon::app().getPlugin<controllers::AssistantBot>()->SessionCount() > 0);
}

DROGON_TEST(AccessFiltersBeforeCommandsAndPreservesBlockedUsersHistory) {
    CHECK(say(20013, "before access rules") == drogon::k204NoContent);
    CHECK(g_sent.Pop(20013, REPLY_WAIT) == "echo: before access rules");
    Json::Value mode{ Json::objectValue };
    mode["mode"] = "allowlist";
    CHECK(admin_request("/api/admin/access", drogon::Put, mode)->statusCode() == drogon::k200OK);

    for (auto const* text : { controllers::HELP_COMMAND, controllers::RESET_COMMAND, "outside allowlist" }) {
        CHECK(say(20013, text) == drogon::k204NoContent);
    }
    CHECK(!g_sent.Pop(20013, NO_REPLY_WAIT).has_value());
    CHECK(!g_requests.Find("outside allowlist").has_value());
    Json::Value qq{ Json::objectValue };
    qq["qq"] = "20013";
    CHECK(admin_request("/api/admin/allowlist", drogon::Post, qq)->statusCode() == drogon::k200OK);
    CHECK(say(20013, "after allowing") == drogon::k204NoContent);
    CHECK(g_sent.Pop(20013, REPLY_WAIT) == "echo: after allowing");
    auto const allowed{ g_requests.Find("after allowing") };
    REQUIRE(allowed.has_value());
    CHECK(describe(*allowed) == std::string{ "system: " } + SYSTEM_PROMPT + " | user: before access rules | assistant: echo: before access rules | user: after allowing");

    CHECK(admin_request("/api/admin/blacklist", drogon::Post, qq)->statusCode() == drogon::k200OK);
    for (auto const* text : { controllers::HELP_COMMAND, controllers::RESET_COMMAND, "in both lists" }) {
        CHECK(say(20013, text) == drogon::k204NoContent);
    }
    Json::Value image{ Json::objectValue };
    image["type"] = "image";
    image["data"]["file"] = "photo.png";
    Json::Value segments{ Json::arrayValue };
    segments.append(image);
    CHECK(test_support::post_event(message_event(20013, segments))->statusCode() == drogon::k204NoContent);
    CHECK(!g_sent.Pop(20013, NO_REPLY_WAIT).has_value());
    CHECK(!g_requests.Find("in both lists").has_value());

    mode["mode"] = "all_friends";
    CHECK(admin_request("/api/admin/access", drogon::Put, mode)->statusCode() == drogon::k200OK);
    CHECK(say(20013, "blacklist in all friends mode") == drogon::k204NoContent);
    CHECK(!g_sent.Pop(20013, NO_REPLY_WAIT).has_value());
    CHECK(!g_requests.Find("blacklist in all friends mode").has_value());
    CHECK(say(20014, "another friend") == drogon::k204NoContent);
    CHECK(g_sent.Pop(20014, REPLY_WAIT) == "echo: another friend");

    mode["mode"] = "allowlist";
    CHECK(admin_request("/api/admin/access", drogon::Put, mode)->statusCode() == drogon::k200OK);
    CHECK(admin_request("/api/admin/blacklist/20013", drogon::Delete)->statusCode() == drogon::k200OK);
    CHECK(say(20013, "after unblocking") == drogon::k204NoContent);
    CHECK(g_sent.Pop(20013, REPLY_WAIT) == "echo: after unblocking");
    auto const unblocked{ g_requests.Find("after unblocking") };
    REQUIRE(unblocked.has_value());
    CHECK(describe(*unblocked) == std::string{ "system: " } + SYSTEM_PROMPT + " | user: before access rules | assistant: echo: before access rules | user: after allowing | assistant: echo: after allowing | user: after unblocking");
    CHECK(say(20013, controllers::HELP_COMMAND) == drogon::k204NoContent);
    CHECK(g_sent.Pop(20013, REPLY_WAIT) == controllers::HELP_REPLY);

    Json::Value pause{ Json::objectValue };
    pause["paused"] = true;
    CHECK(admin_request("/api/admin/bot-status", drogon::Post, pause)->statusCode() == drogon::k200OK);
    CHECK(say(20013, "allowed but paused") == drogon::k204NoContent);
    CHECK(!g_sent.Pop(20013, NO_REPLY_WAIT).has_value());
    CHECK(!g_requests.Find("allowed but paused").has_value());
    pause["paused"] = false;
    CHECK(admin_request("/api/admin/bot-status", drogon::Post, pause)->statusCode() == drogon::k200OK);

    CHECK(admin_request("/api/admin/allowlist/20013", drogon::Delete)->statusCode() == drogon::k200OK);
    CHECK(say(20013, "removed from allowlist") == drogon::k204NoContent);
    CHECK(!g_sent.Pop(20013, NO_REPLY_WAIT).has_value());
    CHECK(!g_requests.Find("removed from allowlist").has_value());
    mode["mode"] = "all_friends";
    CHECK(admin_request("/api/admin/access", drogon::Put, mode)->statusCode() == drogon::k200OK);
}

DROGON_TEST(IgnoresGroupsAndTemporarySessions) {
    auto const group{ message_event(20001, text_message("in a group"), "group", "normal") };
    CHECK(test_support::post_event(group)->statusCode() == drogon::k204NoContent);
    auto const temporary{ message_event(20001, text_message("temporary"), "private", "group") };
    CHECK(test_support::post_event(temporary)->statusCode() == drogon::k204NoContent);

    CHECK(!g_sent.Pop(20001, NO_REPLY_WAIT).has_value());
    for (auto const* text : { "in a group", "temporary" }) {
        CHECK(!g_requests.Find(text).has_value());
    }
}

DROGON_TEST(ChatRecordsIncludeCommandsIgnoredMessagesAndModelFailureNotices) {
    CHECK(say(20101, "fail") == drogon::k204NoContent);
    CHECK(g_sent.Pop(20101, REPLY_WAIT) == workflow::FAILURE_REPLY);
    CHECK(say(20101, controllers::HELP_COMMAND) == drogon::k204NoContent);
    CHECK(g_sent.Pop(20101, REPLY_WAIT) == controllers::HELP_REPLY);
    auto const pause{ drogon::app().getPlugin<controllers::AssistantBot>() };
    pause->SetPaused(true);
    CHECK(say(20101, "ignored during pause") == drogon::k204NoContent);
    auto const records{ wait_messages(20101, 5)["messages"] };
    pause->SetPaused(false);
    REQUIRE(records.size() == 5);
    CHECK(records[0]["content"] == "fail");
    CHECK(records[0]["direction"] == "incoming");
    CHECK(records[1]["content"] == workflow::FAILURE_REPLY);
    CHECK(records[1]["status"] == "sent");
    CHECK(records[2]["content"] == controllers::HELP_COMMAND);
    CHECK(records[3]["content"] == controllers::HELP_REPLY);
    CHECK(records[4]["status"] == "ignored");
    CHECK(records[4]["error"] == "paused");
    CHECK(!g_requests.Find("ignored during pause"));
    CHECK(pause->Chat()->Session(20101)->history.empty());
}

DROGON_TEST(FailedOneBotDeliveriesRemainInTheMessageHistory) {
    CHECK(say(20102, "send failure") == drogon::k204NoContent);
    CHECK(g_sent.Pop(20102, REPLY_WAIT) == "echo: send failure");
    auto const records{ wait_messages(20102, 2)["messages"] };
    REQUIRE(records.size() == 2);
    CHECK(records[1]["content"] == "echo: send failure");
    CHECK(records[1]["direction"] == "outgoing");
    CHECK(records[1]["status"] == "failed");
    CHECK(!records[1]["error"].asString().empty());
}

DROGON_TEST(RecordsTypedDeepSeekAndZeroCacheTokens) {
    for (auto const* prompt : { "typed cache", "deepseek cache", "zero cache" }) {
        CHECK(say(20103, prompt) == drogon::k204NoContent);
        CHECK(g_sent.Pop(20103, REPLY_WAIT) == std::string{ "echo: " } + prompt);
    }
    wait_messages(20103, 6);
    auto const db{ drogon::app().getPlugin<storage::Database>()->Client() };
    auto const rows{ db->execSqlSync("SELECT cached_tokens FROM model_usage WHERE user_id = 20103 ORDER BY id") };
    REQUIRE(rows.size() == 3);
    CHECK(rows[0]["cached_tokens"].as<std::int64_t>() == 3);
    CHECK(rows[1]["cached_tokens"].as<std::int64_t>() == 4);
    CHECK(rows[2]["cached_tokens"].as<std::int64_t>() == 0);
}

DROGON_TEST(HotSettingsChangeRequestModelPromptTokensAndHistory) {
    CHECK(say(20104, "hot first") == drogon::k204NoContent);
    CHECK(g_sent.Pop(20104, REPLY_WAIT) == "echo: hot first");
    Json::Value settings{ Json::objectValue };
    settings["system_prompt"] = "hot prompt";
    settings["model"] = "alternate-model";
    settings["max_tokens"] = 17;
    settings["history_turns"] = 1;
    REQUIRE(admin_request("/api/admin/bot-config", drogon::Put, settings)->statusCode() == drogon::k200OK);
    CHECK(say(20104, "hot second") == drogon::k204NoContent);
    CHECK(g_sent.Pop(20104, REPLY_WAIT) == "echo: hot second");
    auto const params{ g_requests.Params("hot second") };
    REQUIRE(params.has_value());
    CHECK((*params)["model"] == "alternate-model");
    CHECK((*params)["max_tokens"].asInt() == 17);
    CHECK(describe((*params)["messages"]) == "system: hot prompt | user: hot first | assistant: echo: hot first | user: hot second");
    settings["history_turns"] = 0;
    REQUIRE(admin_request("/api/admin/bot-config", drogon::Put, settings)->statusCode() == drogon::k200OK);
    CHECK(say(20104, "hot third") == drogon::k204NoContent);
    CHECK(g_sent.Pop(20104, REPLY_WAIT) == "echo: hot third");
    auto const no_history{ g_requests.Find("hot third") };
    REQUIRE(no_history.has_value());
    CHECK(describe(*no_history) == "system: hot prompt | user: hot third");
    REQUIRE(admin_request("/api/admin/bot-config", drogon::Put, Json::Value{ Json::objectValue })->statusCode() == drogon::k200OK);
    CHECK(say(20104, "hot restored") == drogon::k204NoContent);
    CHECK(g_sent.Pop(20104, REPLY_WAIT) == "echo: hot restored");
    auto const restored{ g_requests.Params("hot restored") };
    REQUIRE(restored.has_value());
    CHECK((*restored)["model"] == "fake-model");
    CHECK(describe((*restored)["messages"]) == std::string{ "system: " } + SYSTEM_PROMPT + " | user: hot restored");
}

DROGON_TEST(RemoteResetClearsPendingAndRejectsTheInflightContext) {
    CHECK(say(20106, "slow remote reset") == drogon::k204NoContent);
    auto const deadline{ std::chrono::steady_clock::now() + REPLY_WAIT };
    while (!g_requests.Find("slow remote reset") && std::chrono::steady_clock::now() < deadline) {
        std::this_thread::sleep_for(std::chrono::milliseconds{ 10 });
    }
    REQUIRE(g_requests.Find("slow remote reset").has_value());
    CHECK(say(20106, "discard queued") == drogon::k204NoContent);
    // A detail request follows the queued message's storage operation on the single connection.
    auto const detail{ admin_request("/api/admin/sessions/20106", drogon::Get) };
    REQUIRE(detail->statusCode() == drogon::k200OK);
    CHECK((*detail->getJsonObject())["busy"].asBool());
    auto const reset{ admin_request("/api/admin/sessions/20106/reset", drogon::Post) };
    REQUIRE(reset->statusCode() == drogon::k200OK);
    CHECK((*reset->getJsonObject())["history"].empty());
    CHECK(g_sent.Pop(20106, REPLY_WAIT) == "echo: slow remote reset");
    CHECK(!g_sent.Pop(20106, NO_REPLY_WAIT));
    CHECK(!g_requests.Find("discard queued"));
    CHECK(say(20106, "after remote reset") == drogon::k204NoContent);
    CHECK(g_sent.Pop(20106, REPLY_WAIT) == "echo: after remote reset");
    auto const next{ g_requests.Find("after remote reset") };
    REQUIRE(next.has_value());
    CHECK(describe(*next) == std::string{ "system: " } + SYSTEM_PROMPT + " | user: after remote reset");
    auto const messages{ wait_messages(20106, 5)["messages"] };
    CHECK(messages[1]["content"] == "discard queued");
}

auto main(int argc, char** argv) -> int {
    test_support::load_config();
    serve_fakes();
    return test_support::run_tests_with_app(argc, argv, drain_chat);
}
