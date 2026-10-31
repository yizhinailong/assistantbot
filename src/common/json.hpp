/**
 * @file json.hpp
 * @brief Share bounded JSON serialization and decoding for agent tools.
 */
#pragma once
#include <memory>
#include <stdexcept>
#include <string>

#include <json/json.h>

namespace common {
    /**
     * @brief Encode a JSON value without indentation.
     * @param value Value to encode.
     * @return Compact UTF-8 JSON.
     */
    inline auto json_text(Json::Value const& value) -> std::string {
        Json::StreamWriterBuilder writer{};
        writer["indentation"] = "";
        return Json::writeString(writer, value);
    }

    /**
     * @brief Decode bounded JSON with at most 32 nested values.
     * @param text Input JSON, at most 1 MiB.
     * @return Parsed value.
     * @throws std::invalid_argument For malformed, oversized, or deeply nested JSON.
     */
    inline auto parse_json(std::string const& text) -> Json::Value {
        if (text.size() > 1024 * 1024) {
            throw std::invalid_argument{ "JSON exceeds 1 MiB" };
        }
        Json::CharReaderBuilder builder{};
        builder["stackLimit"] = 32;
        builder["failIfExtra"] = true;
        builder["rejectDupKeys"] = true;
        builder["collectComments"] = false;
        std::unique_ptr<Json::CharReader> reader{ builder.newCharReader() };
        Json::Value value{};
        std::string errors{};
        try {
            if (reader->parse(text.data(), text.data() + text.size(), &value, &errors)) {
                return value;
            }
        } catch (Json::Exception const&) {}
        throw std::invalid_argument{ "expected valid JSON with at most 32 nesting levels" };
    }
} // namespace common
