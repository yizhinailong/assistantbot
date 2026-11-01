/**
 * @file web.hpp
 * @brief Share bounded web search and page fetch requests for the built-in web tools.
 */
#pragma once

#include <cstddef>
#include <string>

#include <drogon/utils/coroutine.h>
#include <json/value.h>

namespace tools {
    /// Results requested at most for one search.
    constexpr int MAX_WEB_RESULTS{ 10 };
    /// Provider search endpoint used when the feature section leaves it unset.
    constexpr char const* WEB_SEARCH_URL{ "https://api.deepseek.com/v1/tools/search" };
    /// Provider fetch endpoint used when the feature section leaves it unset.
    constexpr char const* WEB_FETCH_URL{ "https://api.deepseek.com/v1/tools/fetch" };

    /**
     * @brief Search the web through the provider's search API.
     *
     * Sends only the query and an optional original question, and asks for titles, URLs,
     * and snippets without page content. Both parameters and results are bounded.
     * @param endpoint Search API URL.
     * @param api_key Bearer token, or empty to send no authorization.
     * @param query Search query with optional provider operators.
     * @param question Original user question used to improve query understanding.
     * @param count Requested results, from one to `MAX_WEB_RESULTS`.
     * @param budget Serialized tool-result budget; the returned list stays below it.
     * @param session Conversation key for HTTP traces.
     * @return Object with a `results` array of titles, URLs, snippets, and dates.
     * @throws std::invalid_argument If an argument is out of bounds.
     * @throws std::runtime_error On transport failure or a non-success response.
     */
    auto web_search(
        std::string endpoint,
        std::string api_key,
        std::string query,
        std::string question,
        int count,
        std::size_t budget,
        std::string session
    ) -> drogon::Task<Json::Value>;

    /**
     * @brief Read one web page as Markdown through the provider's fetch API.
     *
     * The provider performs the download, so the bot never opens the URL itself.
     * @param endpoint Fetch API URL.
     * @param api_key Bearer token, or empty to send no authorization.
     * @param url HTTP(S) page URL without user information.
     * @param budget Serialized tool-result budget; the returned content stays below it.
     * @param session Conversation key for HTTP traces.
     * @return Object with the requested URL, page title, Markdown content, and truncation flag.
     * @throws std::invalid_argument If the URL or budget is invalid.
     * @throws std::runtime_error On transport failure or a non-success response.
     */
    auto web_fetch(
        std::string endpoint,
        std::string api_key,
        std::string url,
        std::size_t budget,
        std::string session
    ) -> drogon::Task<Json::Value>;
} // namespace tools
