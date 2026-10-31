/**
 * @file character_image.hpp
 * @brief Validate bounded static character reference uploads.
 */
#pragma once
#include <string>

namespace workflow {
    /**
     * @brief Decode a canonical data URL and validate PNG, JPEG, or WebP framing.
     * @param data_url Base64 static image, at most 8 MiB decoded.
     * @return Validated bytes.
     * @throws std::invalid_argument If framing, format, animation, or bounds are invalid.
     */
    auto character_image_bytes(std::string const& data_url) -> std::string;
} // namespace workflow
