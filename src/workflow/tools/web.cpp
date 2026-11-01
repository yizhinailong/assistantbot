/**
 * @file web.cpp
 * @brief Call bounded web search and page fetch endpoints without exposing credentials.
 */
#include "workflow/tools/web.hpp"

#include <regex>
#include <stdexcept>
#include <string_view>
#include <utility>

#include "common/json.hpp"
#include "common/text.hpp"
#include "workflow/tools/http.hpp"

namespace tools {
    namespace {

        /// Overall timeout of one provider request in seconds.
        constexpr double WEB_TIMEOUT{ 30.0 };
        /// Longest error text returned to the model.
        constexpr std::size_t MAX_ERROR_CHARS{ 500 };

        /**
         * @brief Truncate text at a UTF-8 character boundary.
         * @param text Source text.
         * @param maximum Maximum characters.
         * @return First bounded part without surrounding whitespace.
         */
        auto bounded_text(std::string_view text, std::size_t maximum) -> std::string {
            if (text.size() <= maximum) {
                return std::string{ text };
            }
            auto parts{ common::split_reply(text, maximum) };
            return parts.empty() ? std::string{} : std::move(parts.front());
        }

        /**
         * @brief Read a string member, treating absent and non-string values as empty.
         * @param value Provider field.
         * @return Member text or an empty string.
         */
        auto text_of(Json::Value const& value) -> std::string {
            return value.isString() ? value.asString() : std::string{};
        }

        /**
         * @brief Read the UTF-8 sequence length of a lead byte.
         * @param lead First byte of a code point.
         * @return One to four bytes; one for an invalid lead byte.
         */
        auto utf8_length(unsigned char lead) -> std::size_t {
            if (lead < 0x80) {
                return 1;
            }
            if (lead < 0xE0) {
                return 2;
            }
            if (lead < 0xF0) {
                return 3;
            }
            return lead < 0xF8 ? 4 : 1;
        }

        /**
         * @brief Keep the longest prefix whose JSON text fits an escaped-size allowance.
         *
         * Serialized tool results escape non-ASCII code points, so a character limit in
         * bytes would let a Chinese page exceed the tool-result budget many times over.
         * @param text Source text.
         * @param allowance Maximum escaped bytes, excluding surrounding quotes.
         * @return Prefix cut at a code point boundary.
         */
        auto escaped_prefix(std::string_view text, std::size_t allowance) -> std::string {
            std::size_t used{};
            std::size_t index{};
            while (index < text.size()) {
                auto const length{ utf8_length(static_cast<unsigned char>(text[index])) };
                if (index + length > text.size()) {
                    break;
                }
                std::size_t cost{};
                if (length == 1) {
                    auto const byte{ static_cast<unsigned char>(text[index]) };
                    if (byte == '"' || byte == '\\') {
                        cost = 2;
                    } else {
                        cost = byte < 0x20 ? 6 : 1;
                    }
                } else {
                    cost = length == 4 ? 12 : 6;
                }
                if (used + cost > allowance) {
                    break;
                }
                used += cost;
                index += length;
            }
            return std::string{ text.substr(0, index) };
        }

        /**
         * @brief Build the error reported for a failed provider response.
         * @param response Provider response.
         * @return Error carrying the provider message when the body has one.
         */
        auto provider_error(HttpResult const& response) -> std::runtime_error {
            std::string message{ "web request failed with HTTP " + std::to_string(response.status) };
            try {
                auto const body{ common::parse_json(response.body) };
                if (body["error"]["message"].isString() && !body["error"]["message"].asString().empty()) {
                    message = "web request failed: " + body["error"]["message"].asString();
                }
            } catch (std::exception const&) {
            }
            return std::runtime_error{ bounded_text(message, MAX_ERROR_CHARS) };
        }

        /**
         * @brief Send one JSON request to a provider endpoint.
         * @param endpoint Provider URL.
         * @param api_key Bearer token, or empty to send no authorization.
         * @param body Owned request object.
         * @param session Conversation key for HTTP traces.
         * @return Bounded successful response; throws on failure or a non-200 status.
         */
        auto post_json(
            std::string const& endpoint,
            std::string const& api_key,
            Json::Value body,
            std::string session
        ) -> drogon::Task<HttpResult> {
            Json::Value headers{ Json::objectValue };
            headers["content-type"] = "application/json";
            if (!api_key.empty()) {
                headers["authorization"] = "Bearer " + api_key;
            }
            auto response{ co_await http_request(endpoint, "POST", common::json_text(body), std::move(headers), WEB_TIMEOUT, "tool", std::move(session)) };
            if (response.status != 200) {
                throw provider_error(response);
            }
            co_return response;
        }

        /**
         * @brief Encode a list entry within the remaining result budget.
         * @param entry Candidate result entry.
         * @return Bytes the entry adds to the serialized tool result.
         */
        auto entry_bytes(Json::Value const& entry) -> std::size_t {
            return common::json_text(entry).size() + 2;
        }
    } // namespace

    auto web_search(
        std::string endpoint,
        std::string api_key,
        std::string query,
        std::string question,
        int count,
        std::size_t budget,
        std::string session
    ) -> drogon::Task<Json::Value> {
        if (query.empty() || query.size() > 2048 || count < 1 || count > MAX_WEB_RESULTS || budget < 512) {
            throw std::invalid_argument{ "invalid web search request" };
        }
        Json::Value body{};
        body["query"] = std::move(query);
        body["count"] = count;
        body["need_content"] = false;
        if (!question.empty()) {
            body["ori_question"] = std::move(question);
        }
        auto const response{ co_await post_json(endpoint, api_key, std::move(body), std::move(session)) };
        auto const parsed{ common::parse_json(response.body) };
        Json::Value results{ Json::arrayValue };
        std::size_t used{ 64 };
        std::size_t const limit{ budget > 64 ? budget - 32 : budget };
        for (auto const& item : parsed["results"]) {
            if (!item.isObject() || results.size() >= static_cast<unsigned>(count)) {
                break;
            }
            auto const url{ bounded_text(text_of(item["url"]), 512) };
            if (url.empty()) {
                continue;
            }
            auto snippet{ text_of(item["snippet"]) };
            if (snippet.empty()) {
                snippet = text_of(item["summary"]);
            }
            Json::Value entry{};
            entry["title"] = bounded_text(text_of(item["title"]), 120);
            entry["url"] = url;
            entry["snippet"] = bounded_text(snippet, 300);
            if (item["date"].isString()) {
                entry["date"] = item["date"];
            }
            auto const size{ entry_bytes(entry) };
            if (used + size > limit) {
                break;
            }
            used += size;
            results.append(std::move(entry));
        }
        Json::Value value{};
        value["results"] = std::move(results);
        co_return value;
    }

    auto web_fetch(
        std::string endpoint,
        std::string api_key,
        std::string url,
        std::size_t budget,
        std::string session
    ) -> drogon::Task<Json::Value> {
        static std::regex const PAGE{ R"(https?://[^\s/@?#]+(?:/[^\s#]*)?)" };
        if (url.size() > 8192 || !std::regex_match(url, PAGE) || budget < 512) {
            throw std::invalid_argument{ "web fetch requires an HTTP(S) URL" };
        }
        Json::Value body{};
        body["url"] = url;
        auto const response{ co_await post_json(endpoint, api_key, std::move(body), std::move(session)) };
        auto const parsed{ common::parse_json(response.body) };
        auto const content{ text_of(parsed["content"]) };
        std::size_t const limit{ budget > 128 ? budget - 32 : budget };
        Json::Value value{};
        value["url"] = url;
        value["title"] = bounded_text(text_of(parsed["title"]), 120);
        value["content"] = "";
        value["truncated"] = false;
        auto const overhead{ common::json_text(value).size() };
        auto const allowance{ limit > overhead ? limit - overhead : 0 };
        auto prefix{ escaped_prefix(content, allowance) };
        value["truncated"] = prefix.size() < content.size();
        value["content"] = std::move(prefix);
        co_return value;
    }

} // namespace tools
