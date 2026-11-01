/**
 * @file test_image_source.cpp
 * @brief Verify bounded conversion of received images into vision request locations.
 */
#include <filesystem>
#include <fstream>
#include <string>

#include <drogon/drogon_test.h>
#include <drogon/utils/Utilities.h>

#include "support.hpp"
#include "workflow/image_source.hpp"

namespace {

    /**
     * @brief Build a tiny image with a supported signature.
     * @param signature Leading bytes that identify the format.
     * @return Image bytes.
     */
    auto image(std::string signature) -> std::string {
        return signature + "payload";
    }

} // namespace

DROGON_TEST(EncodesSupportedImagesAsDataUrls) {
    struct Sample {
        std::string bytes;  ///< Image bytes.
        std::string prefix; ///< Expected data URL prefix.
    };

    std::string const png{ image(std::string{ "\x89PNG\r\n\x1a\n", 8 }) };
    std::string const jpeg{ image(std::string{ "\xff\xd8\xff", 3 }) };
    std::string const gif{ image("GIF89a") };
    std::string const webp{ std::string{ "RIFF\x00\x00\x00\x00WEBP", 12 } + "payload" };
    for (auto const& sample : { Sample{ png, "data:image/png;base64," }, Sample{ jpeg, "data:image/jpeg;base64," }, Sample{ gif, "data:image/gif;base64," }, Sample{ webp, "data:image/webp;base64," } }) {
        auto const encoded{ drogon::utils::base64Encode(sample.bytes) };
        auto const url{ workflow::image_data_url(encoded) };
        REQUIRE(url.has_value());
        CHECK(*url == sample.prefix + encoded);
    }
}

DROGON_TEST(RejectsMalformedAndUnsupportedPayloads) {
    CHECK(!workflow::image_data_url("").has_value());
    CHECK(!workflow::image_data_url("not base64!!").has_value());
    CHECK(!workflow::image_data_url(drogon::utils::base64Encode("plain text")).has_value());
}

DROGON_TEST(OnlyHttpAndImageDataUrlsAreVisionLocations) {
    CHECK(workflow::vision_image_url("https://example.test/a.png"));
    CHECK(workflow::vision_image_url("http://example.test/a.png"));
    CHECK(workflow::vision_image_url("data:image/png;base64,AAAA"));
    CHECK(workflow::vision_image_url("data:image/jpeg;base64,AAAA"));
    CHECK(workflow::vision_image_url("data:image/gif;base64,AAAA"));
    CHECK(workflow::vision_image_url("data:image/webp;base64,AAAA"));
    CHECK(!workflow::vision_image_url("base64://AAAA"));
    CHECK(!workflow::vision_image_url("file:///tmp/a.png"));
    CHECK(!workflow::vision_image_url("data:image/png;base64,"));
    CHECK(!workflow::vision_image_url("data:text/plain;base64,AAAA"));
}

DROGON_TEST(ReadsAbsoluteLocalImagesOnly) {
    auto const directory{ test_support::fresh_directory("image-source") };
    auto const path{ directory / "image.png" };
    {
        std::ofstream file{ path, std::ios::binary };
        file << image(std::string{ "\x89PNG\r\n\x1a\n", 8 });
    }
    CHECK(workflow::image_file_url(path.string()).has_value());
    CHECK(workflow::image_file_url("file://" + path.string()).has_value());
    CHECK(!workflow::image_file_url("image.png").has_value());
    CHECK(!workflow::image_file_url((directory / "missing.png").string()).has_value());
    auto const text{ directory / "image.txt" };
    {
        std::ofstream file{ text, std::ios::binary };
        file << "plain text";
    }
    CHECK(!workflow::image_file_url(text.string()).has_value());
}

auto main(int argc, char** argv) -> int {
    return drogon::test::run(argc, argv);
}
