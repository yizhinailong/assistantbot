/**
 * @file process_metrics.cpp
 * @brief Implement process resource sampling for the runtime administration API.
 */
#include "admin/process_metrics.hpp"

#include <chrono>
#include <fstream>
#include <sstream>
#include <string>
#include <string_view>

#if defined(_WIN32)
    #ifndef NOMINMAX
        #define NOMINMAX
    #endif
    #include <psapi.h>
    #include <tlhelp32.h>
    #include <windows.h>
#endif

namespace admin {

    namespace {

        /**
         * @brief Store an optional unsigned integer or JSON null.
         * @param object Object to modify.
         * @param key Member name.
         * @param value Optional value.
         */
        auto set_optional(
            Json::Value& object,
            char const* key,
            std::optional<std::uint64_t> value
        ) -> void {
            if (value) {
                object[key] = Json::UInt64{ *value };
            } else {
                object[key] = Json::Value{};
            }
        }

#if defined(__linux__)
        /**
         * @brief Read one numeric field from `/proc/self/status`.
         * @param key Field name including the trailing colon.
         * @param kilobytes Whether the value is expressed in KiB.
         * @return Value converted to bytes when requested, or empty when unavailable.
         */
        auto read_status_value(
            std::string_view key,
            bool kilobytes
        ) -> std::optional<std::uint64_t> {
            std::ifstream status{ "/proc/self/status" };
            std::string line{};
            while (std::getline(status, line)) {
                if (!line.starts_with(key)) {
                    continue;
                }
                std::istringstream fields{ line.substr(key.size()) };
                std::uint64_t value{};
                if (!(fields >> value)) {
                    return std::nullopt;
                }
                return kilobytes ? value * 1024 : value;
            }
            return std::nullopt;
        }
#endif

    } // namespace

    /**
     * @brief Sample the current process without fabricating unsupported values.
     * @return Process resource sample with the current Unix timestamp.
     */
    auto sample_process_metrics() -> ProcessMetric {
        ProcessMetric sample{
            .timestamp = std::chrono::duration_cast<std::chrono::seconds>(
                             std::chrono::system_clock::now().time_since_epoch()
            )
                             .count(),
        };
#if defined(__linux__)
        sample.rss_bytes = read_status_value("VmRSS:", true);
        sample.peak_rss_bytes = read_status_value("VmHWM:", true);
        sample.threads = read_status_value("Threads:", false);
#elif defined(_WIN32)
        PROCESS_MEMORY_COUNTERS counters{};
        counters.cb = sizeof(counters);
        if (GetProcessMemoryInfo(GetCurrentProcess(), &counters, sizeof(counters)) != 0) {
            sample.rss_bytes = counters.WorkingSetSize;
            sample.peak_rss_bytes = counters.PeakWorkingSetSize;
        }
        auto const snapshot{ CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0) };
        if (snapshot != INVALID_HANDLE_VALUE) {
            PROCESSENTRY32 entry{};
            entry.dwSize = sizeof(entry);
            if (Process32First(snapshot, &entry) != 0) {
                do {
                    if (entry.th32ProcessID == GetCurrentProcessId()) {
                        sample.threads = entry.cntThreads;
                        break;
                    }
                } while (Process32Next(snapshot, &entry) != 0);
            }
            CloseHandle(snapshot);
        }
#endif
        return sample;
    }

    /**
     * @brief Serialize a process resource sample for the administration API.
     * @param sample Sample to serialize.
     * @return JSON object with null for unsupported fields.
     */
    auto process_metric_json(ProcessMetric const& sample) -> Json::Value {
        Json::Value object{ Json::objectValue };
        object["timestamp"] = Json::Int64{ sample.timestamp };
        set_optional(object, "rss_bytes", sample.rss_bytes);
        set_optional(object, "peak_rss_bytes", sample.peak_rss_bytes);
        set_optional(object, "threads", sample.threads);
        return object;
    }

} // namespace admin
