/**
 * @file usage.cpp
 * @brief Persist model requests and query their daily usage totals.
 */
#include "storage/usage.hpp"

#include <string>

#include "storage/query.hpp"

namespace {

    /**
     * @brief Shared aggregates for totals and grouped usage queries.
     */
    std::string const AGGREGATES{ "count(*) AS requests, coalesce(sum(succeeded), 0) AS successful_requests, count(total_tokens) AS reported_requests, coalesce(sum(prompt_tokens), 0) AS prompt_tokens, coalesce(sum(completion_tokens), 0) AS completion_tokens, coalesce(sum(total_tokens), 0) AS total_tokens, count(cached_tokens) AS cached_requests, coalesce(sum(cached_tokens), 0) AS cached_tokens" };

    /**
     * @brief Serialize aggregate counters with zero values for an empty bucket.
     * @param row Aggregated database row.
     * @return Counter object.
     */
    auto counters(drogon::orm::Row const& row) -> Json::Value {
        Json::Value body{ Json::objectValue };
        for (auto const* key : { "requests", "successful_requests", "reported_requests", "prompt_tokens", "completion_tokens", "total_tokens", "cached_requests", "cached_tokens" }) {
            body[key] = Json::Int64{ row[key].as<std::int64_t>() };
        }
        return body;
    }

} // namespace

namespace storage {

    /**
     * @brief Append a model request to the database.
     * @param db Database client.
     * @param record Request to append.
     * @return Task that ends when the record is saved.
     * @throws drogon::orm::DrogonDbException If saving fails.
     */
    auto record_usage(drogon::orm::DbClientPtr db, UsageRecord record) -> drogon::Task<> {
        co_await query(db, "INSERT INTO model_usage (timestamp, user_id, model, succeeded, prompt_tokens, completion_tokens, total_tokens, cached_tokens) VALUES (?, ?, ?, ?, ?, ?, ?, ?)", record.timestamp, record.user_id, record.model, record.succeeded, record.prompt_tokens, record.completion_tokens, record.total_tokens, record.cached_tokens);
    }

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
    ) -> drogon::Task<Json::Value> {
        auto const result{ co_await query(db, "SELECT " + AGGREGATES + " FROM model_usage WHERE timestamp >= ? AND timestamp < ?", from, to) };
        auto body{ counters(result.at(0)) };
        body["from"] = Json::Int64{ from };
        body["to"] = Json::Int64{ to };
        body["timezone"] = "UTC";
        co_return body;
    }

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
    ) -> drogon::Task<Json::Value> {
        // One statement gives every bucket the same SQLite read snapshot.
        auto const rows{ co_await query(db, "WITH filtered AS (SELECT * FROM model_usage WHERE timestamp >= ? AND timestamp < ?) " "SELECT 'total' AS kind, 0 AS bucket, " + AGGREGATES + " FROM filtered UNION ALL " "SELECT 'today', 0, " + AGGREGATES + " FROM model_usage WHERE timestamp >= ? AND timestamp < ? UNION ALL " "SELECT 'day', timestamp / 86400 * 86400, " + AGGREGATES + " FROM filtered GROUP BY timestamp / 86400 UNION ALL " "SELECT 'user', user_id, " + AGGREGATES + " FROM filtered GROUP BY user_id ORDER BY kind, bucket", from, to, today, today + 86400) };
        Json::Value body{ Json::objectValue };
        body["from"] = Json::Int64{ from };
        body["to"] = Json::Int64{ to };
        body["timezone"] = "UTC";
        body["days"] = Json::Value{ Json::arrayValue };
        body["users"] = Json::Value{ Json::arrayValue };
        for (auto const& row : rows) {
            auto value{ counters(row) };
            auto const kind{ row["kind"].as<std::string>() };
            if (kind == "day") {
                value["timestamp"] = Json::Int64{ row["bucket"].as<std::int64_t>() };
                body["days"].append(std::move(value));
            } else if (kind == "user") {
                value["user_id"] = std::to_string(row["bucket"].as<std::int64_t>());
                body["users"].append(std::move(value));
            } else {
                body[kind] = std::move(value);
            }
        }
        body["today"]["from"] = Json::Int64{ today };
        body["today"]["to"] = Json::Int64{ today + 86400 };
        body["today"]["timezone"] = "UTC";
        co_return body;
    }

} // namespace storage
