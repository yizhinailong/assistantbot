/**
 * @file text.cpp
 * @brief Implement the text conversions between QQ messages and model replies.
 */
#include "common/text.hpp"

#include <algorithm>
#include <format>
#include <ranges>

namespace common {

    namespace {

        constexpr std::string_view WHITESPACE{ " \t\r\n" }; ///< Characters that `trim()` removes.
        constexpr std::size_t MAX_HEADING_LEVEL{ 6 };       ///< Most `#` markers that start a Markdown heading.

        /**
         * @brief Remove leading and trailing whitespace.
         * @param text Text to trim.
         * @return View into `text` without the surrounding whitespace.
         */
        auto trim(std::string_view text) -> std::string_view {
            auto const first{ text.find_first_not_of(WHITESPACE) };
            if (first == std::string_view::npos) {
                return {};
            }
            auto const last{ text.find_last_not_of(WHITESPACE) };
            return text.substr(first, last - first + 1);
        }

        /**
         * @brief Tell whether a byte starts a UTF-8 code point.
         * @param byte Byte to check.
         * @return False for continuation bytes (`10xxxxxx`), true otherwise.
         */
        auto starts_code_point(char byte) -> bool {
            return (static_cast<unsigned char>(byte) & 0xC0U) != 0x80U;
        }

        /**
         * @brief Count the UTF-8 code points of a text.
         * @param text UTF-8 text.
         * @return Number of code points.
         */
        auto code_points(std::string_view text) -> std::size_t {
            return static_cast<std::size_t>(std::ranges::count_if(text, starts_code_point));
        }

        /**
         * @brief Find the byte length of the first `count` code points.
         * @param text UTF-8 text.
         * @param count Number of code points.
         * @return Byte offset right after them, or `text.size()` when the text is shorter.
         */
        auto prefix_bytes(std::string_view text, std::size_t count) -> std::size_t {
            std::size_t seen{};
            for (std::size_t i{}; i < text.size(); ++i) {
                if (starts_code_point(text[i])) {
                    if (seen == count) {
                        return i;
                    }
                    ++seen;
                }
            }
            return text.size();
        }

        /**
         * @brief View one piece of a `std::views::split` result as a string.
         * @param piece Subrange of a split `std::string_view`.
         * @return The same characters as a view.
         */
        auto as_view(std::ranges::subrange<std::string_view::iterator> piece) -> std::string_view {
            return { piece.begin(), piece.end() };
        }

        /**
         * @brief Remove every occurrence of a marker.
         * @param text Text to edit in place.
         * @param marker Characters to remove.
         */
        auto erase_all(std::string& text, std::string_view marker) -> void {
            for (auto pos{ text.find(marker) }; pos != std::string::npos; pos = text.find(marker, pos)) {
                text.erase(pos, marker.size());
            }
        }

        /**
         * @brief Remove the Markdown markup of one line outside fenced code.
         * @param line Line without its line break.
         * @return The line without heading markers, `**`, and backticks.
         */
        auto strip_line(std::string_view line) -> std::string {
            auto const indent{ line.find_first_not_of(' ') };
            if (indent != std::string_view::npos) {
                auto const rest{ line.substr(indent) };
                auto const level{ rest.find_first_not_of('#') };
                if (level != std::string_view::npos && level >= 1 && level <= MAX_HEADING_LEVEL && rest[level] == ' ') {
                    line = rest.substr(level + 1);
                }
            }
            std::string result{ line };
            erase_all(result, "**");
            erase_all(result, "`");
            return result;
        }

        /**
         * @brief Append the trimmed text as a part unless it is blank.
         * @param parts Parts collected so far.
         * @param text Candidate part.
         */
        auto append_part(std::vector<std::string>& parts, std::string_view text) -> void {
            auto const part{ trim(text) };
            if (!part.empty()) {
                parts.emplace_back(part);
            }
        }

    } // namespace

    /**
     * @brief Describe a received message in one log line.
     *
     * The line is `(<self_id>) 接收 <- 私聊 <name>(<user_id>) <content>` for a private message
     * and `(<self_id>) 接收 <- 群聊 (<group_id>) <name>(<user_id>) <content>` for a group
     * message. The name is the sender's group card when set, otherwise their nickname, and
     * may be empty. In the content, `text` segments appear as they are, `at` segments as
     * `@<qq>`, and other segments as `[<type>]`, e.g. `[image]`.
     * @param event Message event from the OneBot implementation.
     * @return The line.
     */
    auto describe_received(drogon_onebot::MessageEvent const& event) -> std::string {
        std::string content{};
        for (auto const& segment : event.message) {
            if (segment.type == "text" && segment.data["text"].isString()) {
                content += segment.data["text"].asString();
            } else if (segment.type == "at") {
                content += "@" + segment.data["qq"].asString();
            } else {
                content += "[" + segment.type + "]";
            }
        }
        auto const& name{ event.sender.card.empty() ? event.sender.nickname : event.sender.card };
        if (event.message_type == "group") {
            return std::format("({}) 接收 <- 群聊 ({}) {}({}) {}", event.self_id, event.group_id.value_or(0), name, event.user_id, content);
        }
        return std::format("({}) 接收 <- 私聊 {}({}) {}", event.self_id, name, event.user_id, content);
    }

    /**
     * @brief Describe a private message that the bot sent in one log line.
     *
     * The line is `(<self_id>) 发送 -> 私聊 <nickname>(<user_id>) <text>`, in the form of
     * `describe_received()`.
     * @param self_id QQ number of the bot account.
     * @param user_id QQ number of the recipient.
     * @param nickname Nickname of the recipient; may be empty.
     * @param text Text that was sent.
     * @return The line.
     */
    auto describe_sent(
        std::int64_t self_id,
        std::int64_t user_id,
        std::string_view nickname,
        std::string_view text
    ) -> std::string {
        return std::format("({}) 发送 -> 私聊 {}({}) {}", self_id, nickname, user_id, text);
    }

    /**
     * @brief Join the text of a message's `text` segments.
     *
     * Other segments, such as `image`, `face`, or `at`, contribute nothing.
     * @param message Message in array format.
     * @return The joined text without leading or trailing whitespace; empty when the
     * message has no text.
     */
    auto extract_text(drogon_onebot::Message const& message) -> std::string {
        std::string text{};
        for (auto const& segment : message) {
            if (segment.type == "text" && segment.data["text"].isString()) {
                text += segment.data["text"].asString();
            }
        }
        return std::string{ trim(text) };
    }

    /**
     * @brief Remove the Markdown markup that QQ would show literally.
     *
     * Drops code fence lines and the `#` markers of headings, and removes `**` and
     * backticks outside fenced code. The lines inside a fence stay as they are.
     * @param text Model reply.
     * @return Plain text without leading or trailing whitespace.
     */
    auto strip_markdown(std::string_view text) -> std::string {
        std::string result{};
        bool in_fence{ false };
        for (auto const piece : std::views::split(text, '\n')) {
            auto const line{ as_view(piece) };
            if (trim(line).starts_with("```")) {
                in_fence = !in_fence;
                continue;
            }
            result += in_fence ? std::string{ line } : strip_line(line);
            result += '\n';
        }
        return std::string{ trim(result) };
    }

    /**
     * @brief Split a reply into messages of at most `max_chars` characters.
     *
     * Characters are UTF-8 code points. Each part ends at a line break where possible;
     * a line longer than `max_chars` is cut between code points.
     * @param text Reply in UTF-8.
     * @param max_chars Largest number of code points in one part; must be positive.
     * @return Non-empty parts in order, without leading or trailing whitespace; empty
     * when the text is blank.
     */
    auto split_reply(std::string_view text, std::size_t max_chars) -> std::vector<std::string> {
        std::vector<std::string> parts{};
        std::string current{};
        std::size_t current_chars{};
        auto const flush{ [&] {
            append_part(parts, current);
            current.clear();
            current_chars = 0;
        } };

        for (auto const piece : std::views::split(trim(text), '\n')) {
            auto line{ as_view(piece) };
            while (code_points(line) > max_chars) {
                flush();
                auto const bytes{ prefix_bytes(line, max_chars) };
                append_part(parts, line.substr(0, bytes));
                line.remove_prefix(bytes);
            }

            auto const chars{ code_points(line) };
            if (!current.empty() && current_chars + 1 + chars > max_chars) {
                flush();
            }
            if (!current.empty()) {
                current += '\n';
                ++current_chars;
            }
            current += line;
            current_chars += chars;
        }
        flush();
        return parts;
    }

} // namespace common
