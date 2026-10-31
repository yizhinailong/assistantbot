/**
 * @file features.hpp
 * @brief Declare optional capability settings and group administration endpoints.
 */
#pragma once

#include <drogon/HttpController.h>
#include <drogon/utils/coroutine.h>

namespace controllers {
    /**
     * @brief Manage immutable feature sections and opt-in group conversations.
     */
    class FeaturesController : public drogon::HttpController<FeaturesController> {
    public:
        METHOD_LIST_BEGIN
        ADD_METHOD_TO(FeaturesController::Section, "/api/admin/features/{1}", drogon::Get, drogon::Put);
        ADD_METHOD_TO(FeaturesController::Groups, "/api/admin/groups", drogon::Get);
        ADD_METHOD_TO(FeaturesController::Group, "/api/admin/groups/{1}", drogon::Put);
        METHOD_LIST_END

        /**
         * @brief Read or atomically replace a validated optional-feature section.
         * @param req Authenticated request with values and optional decimal revision.
         * @param section Known feature section name.
         * @return Section with defaults and its revision, or an HTTP error.
         */
        auto Section(drogon::HttpRequestPtr req, std::string section) const -> drogon::Task<drogon::HttpResponsePtr>;

        /**
         * @brief Merge typed OneBot group membership with saved opt-in settings.
         * @param req Authenticated request.
         * @return Groups, their configuration, and the current revision.
         */
        auto Groups(drogon::HttpRequestPtr req) const -> drogon::Task<drogon::HttpResponsePtr>;

        /**
         * @brief Save one group's complete configuration without dropping other groups.
         * @param req Authenticated request with values and optional revision.
         * @param qq Positive group QQ number.
         * @return Saved groups section or an HTTP error.
         */
        auto Group(drogon::HttpRequestPtr req, std::string qq) const -> drogon::Task<drogon::HttpResponsePtr>;
    };
} // namespace controllers
