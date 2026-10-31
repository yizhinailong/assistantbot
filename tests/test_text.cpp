/**
 * @file test_text.cpp
 * @brief Check the text conversions between QQ messages and model replies.
 */
#include <string>
#include <utility>
#include <vector>

#include <drogon/drogon_test.h>

#include "common/text.hpp"

namespace {

    /**
     * @brief Build a message segment.
     * @param type Segment type.
     * @param key Name of the single data field; empty leaves `data` empty.
     * @param value Value of that field.
     * @return The segment.
     */
    auto segment(
        std::string type,
        char const* key = "",
        std::string const& value = {}
    ) -> drogon_onebot::MessageSegment {
        drogon_onebot::MessageSegment result{ .type = std::move(type) };
        if (*key != '\0') {
            result.data[key] = value;
        }
        return result;
    }

} // namespace

DROGON_TEST(DescribeReceivedPrivateMessage) {
    drogon_onebot::MessageEvent const event{
        .self_id = 10000,
        .message_type = "private",
        .sub_type = "friend",
        .user_id = 20001,
        .message = { segment("text", "text", "你好"), segment("image", "file", "a.png"), segment("face", "id", "14") },
        .sender = { .user_id = 20001, .nickname = "张三" },
    };
    CHECK(common::describe_received(event) == "(10000) 接收 <- 私聊 张三(20001) 你好[image][face]");
}

DROGON_TEST(DescribeReceivedGroupMessage) {
    drogon_onebot::MessageEvent event{
        .self_id = 10000,
        .message_type = "group",
        .sub_type = "normal",
        .user_id = 20001,
        .group_id = 30001,
        .message = { segment("at", "qq", "10000"), segment("text", "text", " 在吗") },
        .sender = { .user_id = 20001, .nickname = "张三", .card = "群名片" },
    };
    CHECK(common::describe_received(event) == "(10000) 接收 <- 群聊 (30001) 群名片(20001) @10000 在吗");

    event.sender = {};
    CHECK(common::describe_received(event) == "(10000) 接收 <- 群聊 (30001) (20001) @10000 在吗");
}

DROGON_TEST(DescribeSentPrivateMessage) {
    CHECK(common::describe_sent(10000, 20001, "张三", "你好！") == "(10000) 发送 -> 私聊 张三(20001) 你好！");
    CHECK(common::describe_sent(10000, 20001, "", "你好！") == "(10000) 发送 -> 私聊 (20001) 你好！");
}

DROGON_TEST(ExtractTextJoinsTextSegments) {
    drogon_onebot::Message const message{
        segment("text", "text", "  Hello, "),
        segment("face", "id", "14"),
        segment("text", "text", "world!\n"),
    };
    CHECK(common::extract_text(message) == "Hello, world!");
}

DROGON_TEST(ExtractTextIgnoresOtherSegments) {
    drogon_onebot::Message const message{
        segment("image", "file", "a.png"),
        segment("text", "text", "  "),
        segment("text"),
    };
    CHECK(common::extract_text(message).empty());
    CHECK(common::extract_text({}).empty());
}

DROGON_TEST(StripMarkdownRemovesMarkup) {
    CHECK(common::strip_markdown("## Title\n**bold** and `code`\n") == "Title\nbold and code");
    CHECK(common::strip_markdown("  # Indented heading") == "Indented heading");
    CHECK(common::strip_markdown("#hashtag and ####### seven") == "#hashtag and ####### seven");
    CHECK(common::strip_markdown("####### not a heading") == "####### not a heading");
}

DROGON_TEST(StripMarkdownKeepsFencedCode) {
    CHECK(common::strip_markdown("Run:\n```sh\necho `pwd` **x**\n```\nDone.") == "Run:\necho `pwd` **x**\nDone.");
}

DROGON_TEST(SplitReplyKeepsShortText) {
    CHECK(common::split_reply("  short reply \n", 20) == std::vector<std::string>{ "short reply" });
    CHECK(common::split_reply(" \n ", 20).empty());
}

DROGON_TEST(SplitReplyBreaksAtLines) {
    std::vector<std::string> const expected{ "first line\nsecond line", "third" };
    CHECK(common::split_reply("first line\nsecond line\nthird", 22) == expected);
}

DROGON_TEST(SplitReplyCutsLongLines) {
    std::vector<std::string> const expected{ "abcd", "efgh", "ij" };
    CHECK(common::split_reply("abcdefghij", 4) == expected);
}

DROGON_TEST(SplitReplyCountsCodePoints) {
    // Each of these characters takes three bytes in UTF-8.
    std::vector<std::string> const expected{ "你好世界", "和平" };
    CHECK(common::split_reply("你好世界和平", 4) == expected);
}

auto main(int argc, char** argv) -> int {
    return drogon::test::run(argc, argv);
}
