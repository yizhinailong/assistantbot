/**
 * @file usage.hpp
 * @brief Persist model requests and query their daily usage totals.
 */
#pragma once

#include <cstdint>
#include <optional>
#include <string>

#include <drogon/orm/DbClient.h>
#include <drogon/utils/coroutine.h>
#include <json/value.h>

namespace storage {

    /**
     * @brief One completed model request, including failures and missing token reports.
     */
    struct UsageRecord {
        std::int64_t timestamp{};                      ///< Completion time in UTC seconds.
        std::int64_t user_id{};                        ///< Friend who prompted the request.
        std::string model;                             ///< Response model, or configured model on failure.
        bool succeeded{};                              ///< Whether the model returned usable reply text.
        std::optional<std::int64_t> prompt_tokens;     ///< Reported input tokens.
        std::optional<std::int64_t> completion_tokens; ///< Reported output tokens.
        std::optional<std::int64_t> total_tokens;      ///< Reported total tokens.
        std::optional<std::int64_t> cached_tokens;     ///< Reported cached input tokens; null when unavailable.
    };

    /**
     * @brief Append a model request to the database.
     * @param db Database client.
     * @param record Request to append.
     * @return Task that ends when the record is saved.
     * @throws drogon::orm::DrogonDbException If saving fails.
     */
    auto record_usage(drogon::orm::DbClientPtr db, UsageRecord record) -> drogon::Task<>;

    /**
     * @brief Sum model requests in a UTC interval, with exclusive upper bound.
     * @param db Database client.
     * @param from Inclusive UTC timestamp in seconds.
     * @param to Exclusive UTC timestamp in seconds.
     * @return Totals and the number of requests with reported token usage.
     * @throws drogon::orm::DrogonDbException If querying fails.
     */
    auto summarize_usage(
        drogon::orm::DbClientPtr db,
        std::int64_t from,
        std::int64_t to
    ) -> drogon::Task<Json::Value>;

    /**
     * @brief Query UTC usage totals, today's totals, daily bars, and friend rankings.
     * @param db Database client.
     * @param from Inclusive interval start.
     * @param to Exclusive interval end.
     * @param today UTC midnight of the current day.
     * @return Totals and grouped arrays; missing cache reports remain distinguishable.
     * @throws drogon::orm::DrogonDbException If querying fails.
     */
    auto usage_report(
        drogon::orm::DbClientPtr db,
        std::int64_t from,
        std::int64_t to,
        std::int64_t today
    ) -> drogon::Task<Json::Value>;

} // namespace storage
