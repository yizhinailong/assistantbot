/**
 * @file character_image.cpp
 * @brief Reject oversized, truncated, or animated character reference uploads.
 */
#include "workflow/character_image.hpp"

#include <cstdint>
#include <stdexcept>
#include <string_view>

#include <drogon/utils/Utilities.h>

namespace workflow {
    /**
     * @brief Decode a canonical data URL and validate PNG, JPEG, or WebP framing.
     * @param data_url Base64 static image, at most 8 MiB decoded.
     * @return Validated bytes.
     * @throws std::invalid_argument If framing, format, animation, or bounds are invalid.
     */
    auto character_image_bytes(std::string const& data_url) -> std::string {
        auto const comma{ data_url.find(',') };
        if (comma == std::string::npos || comma > 32 || data_url.size() > 12 * 1024 * 1024) {
            throw std::invalid_argument{ "expected a PNG, JPEG, or WebP base64 data URL" };
        }
        auto const prefix{ data_url.substr(0, comma) };
        if (prefix != "data:image/png;base64" && prefix != "data:image/jpeg;base64" && prefix != "data:image/webp;base64") {
            throw std::invalid_argument{ "unsupported character image format" };
        }
        auto const encoded{ std::string_view{ data_url }.substr(comma + 1) };
        auto bytes{ drogon::utils::base64Decode(encoded) };
        if (bytes.empty() || bytes.size() > 8 * 1024 * 1024 || drogon::utils::base64Encode(bytes) != encoded) {
            throw std::invalid_argument{ "image base64 is invalid or exceeds 8 MiB" };
        }
        auto const word{ [&](std::size_t offset, bool little = false) -> std::uint32_t {
            std::uint32_t result{};
            for (std::size_t i{}; i < 4; ++i) {
                result |= static_cast<std::uint32_t>(static_cast<unsigned char>(bytes.at(offset + i))) << ((little ? i : 3 - i) * 8);
            }
            return result;
        } };
        bool valid{};
        if (prefix == "data:image/png;base64" && bytes.starts_with(std::string_view{ "\x89PNG\r\n\x1a\n", 8 })) {
            std::size_t offset{ 8 };
            bool header{};
            bool pixels{};
            while (offset + 12 <= bytes.size()) {
                auto const length{ word(offset) };
                if (length > bytes.size() - offset - 12) {
                    break;
                }
                auto const type{ std::string_view{ bytes }.substr(offset + 4, 4) };
                if (!header && (type != "IHDR" || length != 13)) {
                    break;
                }
                if (type == "IHDR") {
                    if (header || word(offset + 8) == 0 || word(offset + 12) == 0 || word(offset + 8) > 8192 || word(offset + 12) > 8192) {
                        break;
                    }
                    header = true;
                }
                if (type == "acTL" || type == "fcTL" || type == "fdAT") {
                    break;
                }
                if (type == "IDAT") {
                    pixels = pixels || length > 0;
                }
                offset += length + 12;
                if (type == "IEND") {
                    valid = header && pixels && length == 0 && offset == bytes.size();
                    break;
                }
            }
        } else if (prefix == "data:image/jpeg;base64" && bytes.size() >= 12 && bytes.starts_with(std::string_view{ "\xff\xd8\xff", 3 }) && bytes.ends_with(std::string_view{ "\xff\xd9", 2 })) {
            std::size_t offset{ 2 };
            bool frame{};
            while (offset + 4 <= bytes.size()) {
                if (static_cast<unsigned char>(bytes[offset++]) != 0xff) {
                    break;
                }
                auto const marker{ static_cast<unsigned char>(bytes[offset++]) };
                if (marker == 0xff) {
                    --offset;
                    continue;
                }
                if (marker == 0xda) {
                    valid = frame;
                    break;
                }
                auto const length{ (static_cast<unsigned char>(bytes[offset]) << 8) | static_cast<unsigned char>(bytes[offset + 1]) };
                if (length < 2 || static_cast<std::size_t>(length) > bytes.size() - offset) {
                    break;
                }
                if (marker == 0xc0 || marker == 0xc1 || marker == 0xc2) {
                    if (length < 8) {
                        break;
                    }
                    auto const height{ (static_cast<unsigned char>(bytes[offset + 3]) << 8) | static_cast<unsigned char>(bytes[offset + 4]) };
                    auto const width{ (static_cast<unsigned char>(bytes[offset + 5]) << 8) | static_cast<unsigned char>(bytes[offset + 6]) };
                    if (width == 0 || height == 0 || width > 8192 || height > 8192) {
                        break;
                    }
                    frame = true;
                }
                offset += length;
            }
        } else if (prefix == "data:image/webp;base64" && bytes.size() >= 20 && bytes.starts_with("RIFF") && bytes.substr(8, 4) == "WEBP" && word(4, true) == bytes.size() - 8) {
            std::size_t offset{ 12 };
            bool pixels{};
            valid = true;
            while (offset + 8 <= bytes.size()) {
                auto const length{ word(offset + 4, true) };
                auto const type{ bytes.substr(offset, 4) };
                if (length > bytes.size() - offset - 8 || type == "ANIM" || type == "ANMF" || (type == "VP8X" && length >= 1 && (static_cast<unsigned char>(bytes[offset + 8]) & 2))) {
                    valid = false;
                    break;
                }
                pixels = pixels || type == "VP8 " || type == "VP8L";
                offset += 8 + length + (length % 2);
            }
            valid = valid && pixels && offset == bytes.size();
        }
        if (!valid) {
            throw std::invalid_argument{ "character image must be a complete static PNG, JPEG, or WebP" };
        }
        return bytes;
    }
} // namespace workflow
