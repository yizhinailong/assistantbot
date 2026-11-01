/**
 * @file image_source.hpp
 * @brief Declare bounded conversion of received images into vision request locations.
 */
#pragma once

#include <cstddef>
#include <optional>
#include <string>
#include <string_view>

namespace workflow {

    /// Images described for one reply.
    constexpr std::size_t MAX_MESSAGE_IMAGES{ 4 };
    /// Decoded bytes accepted for one image.
    constexpr std::size_t MAX_IMAGE_BYTES{ 8 * 1024 * 1024 };
    /// Encoded bytes accepted for one image data URL.
    constexpr std::size_t MAX_IMAGE_DATA_URL_BYTES{ MAX_IMAGE_BYTES * 4 / 3 + 64 };
    /// Encoded bytes accepted across one reply's images.
    constexpr std::size_t MAX_MESSAGE_IMAGE_DATA_BYTES{ 16 * 1024 * 1024 };

    /**
     * @brief Tell whether a location may be sent to the vision model.
     *
     * Accepts bounded HTTP(S) URLs and data URLs for PNG, JPEG, GIF, or WebP images.
     * @param value Image location.
     * @return Whether the value is a vision-ready location.
     */
    auto vision_image_url(std::string const& value) -> bool;

    /**
     * @brief Convert base64 image data into a canonical data URL.
     * @param encoded Standard padded base64 without a prefix.
     * @return Data URL, or empty when the bytes are invalid, unsupported, or too large.
     */
    auto image_data_url(std::string_view encoded) -> std::optional<std::string>;

    /**
     * @brief Read a local PNG, JPEG, GIF, or WebP file into a data URL.
     *
     * Only `file://` URIs and absolute paths are read, so a relative name from an event
     * cannot select a file in the working directory.
     * @param path Local path or `file://` URI.
     * @return Data URL, or empty when the file is missing, unsupported, or too large.
     */
    auto image_file_url(std::string const& path) -> std::optional<std::string>;

} // namespace workflow
