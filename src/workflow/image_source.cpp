/**
 * @file image_source.cpp
 * @brief Convert bounded base64 and local image files into vision data URLs.
 */
#include "workflow/image_source.hpp"

#include <cctype>
#include <filesystem>
#include <fstream>
#include <regex>

#include <drogon/utils/Utilities.h>

namespace workflow {

    namespace {

        /**
         * @brief Identify a supported image by its leading bytes.
         * @param bytes Image bytes.
         * @return MIME type, or empty when unsupported.
         */
        auto image_mime(std::string_view bytes) -> std::string_view {
            if (bytes.starts_with(std::string_view{ "\x89PNG\r\n\x1a\n", 8 })) {
                return "image/png";
            }
            if (bytes.size() >= 3 && bytes[0] == '\xff' && bytes[1] == '\xd8' && bytes[2] == '\xff') {
                return "image/jpeg";
            }
            if (bytes.starts_with("GIF87a") || bytes.starts_with("GIF89a")) {
                return "image/gif";
            }
            if (bytes.size() >= 12 && bytes.starts_with("RIFF") && bytes.substr(8, 4) == "WEBP") {
                return "image/webp";
            }
            return {};
        }

        /**
         * @brief Wrap image bytes in a data URL.
         * @param bytes Image bytes no larger than `MAX_IMAGE_BYTES`.
         * @return Data URL, or empty when the format is unsupported.
         */
        auto data_url(std::string const& bytes) -> std::optional<std::string> {
            auto const mime{ image_mime(bytes) };
            if (mime.empty()) {
                return std::nullopt;
            }
            return "data:" + std::string{ mime } + ";base64," + drogon::utils::base64Encode(bytes);
        }

        /**
         * @brief Turn a `file://` URI or an absolute path into a local path.
         * @param value Event-supplied location.
         * @return Local path, or empty for relative names and other schemes.
         */
        auto local_path(std::string value) -> std::optional<std::string> {
            if (value.starts_with("file://")) {
                value.erase(0, 7);
                if (value.starts_with("localhost/")) {
                    value.erase(0, 9);
                }
            }
            if (value.empty() || value.size() > 4096 || value.find('\0') != std::string::npos) {
                return std::nullopt;
            }
#ifdef _WIN32
            if (value.starts_with('/') && value.size() >= 3 && std::isalpha(static_cast<unsigned char>(value[1])) && value[2] == ':') {
                value.erase(0, 1);
            }
            bool const absolute{ value.size() >= 3 && std::isalpha(static_cast<unsigned char>(value[0])) && value[1] == ':' && (value[2] == '\\' || value[2] == '/') };
#else
            bool const absolute{ value.starts_with('/') };
#endif
            if (!absolute) {
                return std::nullopt;
            }
            return value;
        }

    } // namespace

    auto vision_image_url(std::string const& value) -> bool {
        static std::regex const HTTP{ R"(https?://[^\s/@?#]+(?:/[^\s#]*)?)" };
        if (value.size() <= 8192 && std::regex_match(value, HTTP)) {
            return true;
        }
        if (value.size() > MAX_IMAGE_DATA_URL_BYTES) {
            return false;
        }
        for (auto const* prefix : {
                 "data:image/png;base64,",
                 "data:image/jpeg;base64,",
                 "data:image/gif;base64,",
                 "data:image/webp;base64," }) {
            if (value.starts_with(prefix) && value.size() > std::char_traits<char>::length(prefix)) {
                return true;
            }
        }
        return false;
    }

    auto image_data_url(std::string_view encoded) -> std::optional<std::string> {
        if (encoded.empty() || encoded.size() > MAX_IMAGE_DATA_URL_BYTES) {
            return std::nullopt;
        }
        auto const bytes{ drogon::utils::base64Decode(encoded) };
        auto const canonical{ drogon::utils::base64Encode(bytes) };
        if (bytes.empty() || bytes.size() > MAX_IMAGE_BYTES || std::string_view{ canonical } != encoded) {
            return std::nullopt;
        }
        return data_url(bytes);
    }

    auto image_file_url(std::string const& path) -> std::optional<std::string> {
        auto const file{ local_path(path) };
        if (!file) {
            return std::nullopt;
        }
        std::error_code error{};
        auto const size{ std::filesystem::file_size(*file, error) };
        if (error || size == 0 || size > MAX_IMAGE_BYTES) {
            return std::nullopt;
        }
        std::ifstream stream{ *file, std::ios::binary };
        if (!stream) {
            return std::nullopt;
        }
        std::string bytes(size, '\0');
        if (!stream.read(bytes.data(), static_cast<std::streamsize>(size))) {
            return std::nullopt;
        }
        return data_url(bytes);
    }

} // namespace workflow
