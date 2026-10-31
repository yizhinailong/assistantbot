
/**
 * @file process_metrics.hpp
 * @brief Declare process resource samples for the runtime administration API.
 */
#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>

#include <json/value.h>

namespace admin {

    constexpr std::size_t PROCESS_METRICS_CAPACITY{ 1440 };    ///< Samples retained for the last 24 hours.
    constexpr double PROCESS_METRICS_INTERVAL_SECONDS{ 60.0 }; ///< Seconds between process samples.

    /**
     * @brief One sample of this process's resource use.
     */
    struct ProcessMetric {
        std::int64_t timestamp{};                    ///< Unix timestamp in seconds.
        std::optional<std::uint64_t> rss_bytes;      ///< Current resident set size, or empty when unsupported.
        std::optional<std::uint64_t> peak_rss_bytes; ///< Peak resident set size, or empty when unsupported.
        std::optional<std::uint64_t> threads;        ///< Current thread count, or empty when unsupported.
    };

    /**
     * @brief Sample the current process without fabricating unsupported values.
     * @return Process resource sample with the current Unix timestamp.
     */
    [[nodiscard]] auto sample_process_metrics() -> ProcessMetric;

    /**
     * @brief Serialize a process resource sample for the administration API.
     * @param sample Sample to serialize.
     * @return JSON object with null for unsupported fields.
     */
    [[nodiscard]] auto process_metric_json(ProcessMetric const& sample) -> Json::Value;

} // namespace admin
