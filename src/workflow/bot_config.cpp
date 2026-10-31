/**
 * @file bot_config.cpp
 * @brief Implement reading the settings of the `controllers::AssistantBot` plugin.
 */
#include "workflow/bot_config.hpp"

#include <chrono>
#include <cmath>
#include <cstddef>
#include <format>
#include <stdexcept>
#include <string_view>

namespace workflow {

    namespace {

        constexpr char const* PLUGIN_NAME{ "controllers::AssistantBot" }; ///< Prefix of every error message.

        /**
         * @brief Build the exception for an invalid config key.
         * @param key Name of the key.
         * @param problem What is wrong, completing a sentence that starts with the key.
         * @return Exception with the message `controllers::AssistantBot: <key> <problem>`.
         */
        auto invalid(std::string_view key, std::string_view problem) -> std::invalid_argument {
            return std::invalid_argument{ std::format("{}: {} {}", PLUGIN_NAME, key, problem) };
        }

        /**
         * @brief Read an optional integer key.
         * @param config Plugin config object, or null.
         * @param key Name of the key.
         * @param min Smallest allowed value.
         * @return The value, or empty when the key is absent.
         * @throws std::invalid_argument If the value is not an integer of at least `min`.
         */
        auto read_integer(
            Json::Value const& config,
            char const* key,
            std::int64_t min
        ) -> std::optional<std::int64_t> {
            if (!config.isMember(key)) {
                return std::nullopt;
            }
            auto const& value{ config[key] };
            if (!value.isInt64() || value.asInt64() < min) {
                throw invalid(key, std::format("must be an integer of at least {}", min));
            }
            return value.asInt64();
        }

    } // namespace

    /**
     * @brief Read and validate the plugin config.
     *
     * | Key | Required | Meaning |
     * | --- | --- | --- |
     * | `system_prompt` | No | String; empty sends no system message |
     * | `history_turns` | No | Integer >= 0, default 10 |
     * | `idle_reset_minutes` | No | Integer >= 1, default 30 |
     * | `request_timeout` | No | Number of seconds > 0, default 90 |
     * | `max_tokens` | No | Integer >= 1 or null; unset uses the endpoint's default |
     * | `max_reply_chars` | No | Integer >= 1, default 1500 |
     * | `model` | No | String; empty uses the model plugin default |
     * @param config The plugin's `config` object; null when config.json gives none.
     * @return Settings, with defaults for the keys that are absent.
     * @throws std::invalid_argument If `config` is not an object, or a key has the wrong type or
     * range. The message starts with `controllers::AssistantBot:` and names the key.
     */
    auto parse_bot_config(Json::Value const& config) -> BotConfig {
        if (!config.isNull() && !config.isObject()) {
            throw invalid("config", "must be an object");
        }
        BotConfig result{};
        if (config.isMember("system_prompt")) {
            if (!config["system_prompt"].isString()) {
                throw invalid("system_prompt", "must be a string");
            }
            result.system_prompt = config["system_prompt"].asString();
        }
        if (config.isMember("request_timeout")) {
            auto const& timeout{ config["request_timeout"] };
            if (!timeout.isNumeric() || !std::isfinite(timeout.asDouble()) || timeout.asDouble() <= 0.0) {
                throw invalid("request_timeout", "must be a positive number of seconds");
            }
            result.request_timeout = timeout.asDouble();
        }
        if (auto const turns{ read_integer(config, "history_turns", 0) }) {
            result.history_turns = static_cast<std::size_t>(*turns);
        }
        if (auto const minutes{ read_integer(config, "idle_reset_minutes", 1) }) {
            auto const largest{ std::chrono::duration_cast<std::chrono::minutes>(std::chrono::steady_clock::duration::max()).count() };
            if (*minutes > largest) {
                throw invalid("idle_reset_minutes", "exceeds the steady clock duration range");
            }
            result.idle_reset = std::chrono::minutes{ *minutes };
        }
        if (!config["max_tokens"].isNull()) {
            result.max_tokens = read_integer(config, "max_tokens", 1);
        }
        if (config.isMember("model")) {
            if (!config["model"].isString()) {
                throw invalid("model", "must be a string");
            }
            result.model = config["model"].asString();
        }
        if (auto const chars{ read_integer(config, "max_reply_chars", 1) }) {
            result.max_reply_chars = static_cast<std::size_t>(*chars);
        }
        return result;
    }

} // namespace workflow
