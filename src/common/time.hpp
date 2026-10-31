/**
 * @file time.hpp
 * @brief Provide UTC timestamps for stored events and administrative queries.
 */
#pragma once

#include <chrono>
#include <cstdint>

namespace common {

    /**
     * @brief Read the current UTC Unix timestamp.
     * @return Seconds since the Unix epoch.
     */
    inline auto unix_seconds() -> std::int64_t {
        return std::chrono::duration_cast<std::chrono::seconds>(std::chrono::system_clock::now().time_since_epoch()).count();
    }

} // namespace common
