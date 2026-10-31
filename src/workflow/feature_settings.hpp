/**
 * @file feature_settings.hpp
 * @brief Persist immutable configuration for groups and optional agent capabilities.
 */
#pragma once

#include <cstdint>
#include <memory>
#include <mutex>
#include <optional>
#include <string>

#include <drogon/orm/DbClient.h>
#include <drogon/utils/coroutine.h>
#include <json/value.h>

namespace workflow {

    /**
     * @brief Return defaults for groups, administrators, model roles, tools, MCP, and memory.
     * @return Complete configuration with optional capabilities disabled.
     */
    auto default_feature_settings() -> Json::Value;

    /**
     * @brief Validate and publish saved feature documents after successful persistence.
     */
    class FeatureSettings {
    private:
        drogon::orm::DbClientPtr m_database;          ///< Settings database.
        mutable std::mutex m_mutex;                   ///< Protects publication and the write claim.
        std::shared_ptr<Json::Value const> m_current; ///< Immutable complete configuration.
        bool m_writing{};                             ///< One save may be active at a time.

    public:
        /**
         * @brief Load saved optional-feature configuration during plugin initialization.
         * @param database Database running on its own connection thread.
         */
        explicit FeatureSettings(drogon::orm::DbClientPtr database);

        /**
         * @brief Copy the current immutable complete configuration pointer.
         * @return Snapshot valid across subsequent updates.
         */
        [[nodiscard]] auto Current() const -> std::shared_ptr<Json::Value const>;

        /**
         * @brief Describe a section and its optimistic concurrency revision.
         * @param section groups, admins, models, execution, memory, tools, or mcp.
         * @return Values, defaults, and decimal revision.
         * @throws std::invalid_argument If the section is unknown.
         */
        [[nodiscard]] auto Describe(std::string const& section) const -> Json::Value;

        /**
         * @brief Validate, save, and publish one complete feature section.
         * @param section Feature section to replace.
         * @param values New section values; omitted optional fields use defaults.
         * @param revision Expected revision, or empty to accept the current revision.
         * @return Task completing after publication.
         * @throws std::invalid_argument If values are invalid.
         * @throws std::runtime_error If a write overlaps or the revision is stale.
         */
        auto Update(
            std::string section,
            Json::Value values,
            std::optional<std::uint64_t> revision = {}
        ) -> drogon::Task<>;
    };

} // namespace workflow
