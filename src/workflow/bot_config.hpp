/**
 * @file bot_config.hpp
 * @brief Declare the settings of the `controllers::AssistantBot` plugin.
 */
#pragma once

#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

#include <json/value.h>

namespace workflow {

    /**
     * @brief System prompt used when the config sets none.
     */
    constexpr char const* DEFAULT_SYSTEM_PROMPT{
        "You are a helpful assistant chatting with the user in QQ private messages. " "Reply in the user's language, in plain text without Markdown, because QQ shows Markdown markup literally."
    };

    /**
     * @brief Keys that `parse_bot_config()` reads, in the order of its table.
     */
    constexpr std::array<std::string_view, 7> BOT_CONFIG_KEYS{
        "system_prompt",
        "history_turns",
        "idle_reset_minutes",
        "request_timeout",
        "max_tokens",
        "max_reply_chars",
        "model"
    };

    /**
     * @brief Settings of the private chat bot, read from the plugin's `config` in config.json.
     */
    struct BotConfig {
        std::string system_prompt{ DEFAULT_SYSTEM_PROMPT }; ///< First message of every model request; empty sends none.
        std::size_t history_turns{ 10 };                    ///< Most turns per user sent back to the model.
        std::chrono::minutes idle_reset{ 30 };              ///< Silence after which a user's history is cleared.
        double request_timeout{ 90.0 };                     ///< Seconds to wait for the model.
        std::optional<std::int64_t> max_tokens{};           ///< Upper bound on generated tokens; unset uses the endpoint's default.
        std::string model;                                  ///< Per-request model override; empty uses the model plugin default.
        std::size_t max_reply_chars{ 1500 };                ///< Most characters in one QQ message; longer replies are split.
    };

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
    auto parse_bot_config(Json::Value const& config) -> BotConfig;

} // namespace workflow
