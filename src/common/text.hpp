/**
 * @file text.hpp
 * @brief Declare the text conversions between QQ messages and model replies.
 */
#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include <drogon_onebot/types/event.hpp>
#include <drogon_onebot/types/message.hpp>

namespace common {

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
    auto describe_received(drogon_onebot::MessageEvent const& event) -> std::string;

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
    ) -> std::string;

    /**
     * @brief Join the text of a message's `text` segments.
     *
     * Other segments, such as `image`, `face`, or `at`, contribute nothing.
     * @param message Message in array format.
     * @return The joined text without leading or trailing whitespace; empty when the
     * message has no text.
     */
    auto extract_text(drogon_onebot::Message const& message) -> std::string;

    /**
     * @brief Remove the Markdown markup that QQ would show literally.
     *
     * Drops code fence lines and the `#` markers of headings, and removes `**` and
     * backticks outside fenced code. The lines inside a fence stay as they are.
     * @param text Model reply.
     * @return Plain text without leading or trailing whitespace.
     */
    auto strip_markdown(std::string_view text) -> std::string;

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
    auto split_reply(std::string_view text, std::size_t max_chars) -> std::vector<std::string>;

} // namespace common
