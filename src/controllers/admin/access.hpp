/**
 * @file access.hpp
 * @brief Declare authenticated access policy and friend list endpoints.
 */
#pragma once

#include <string>

#include <drogon/HttpController.h>
#include <drogon/utils/coroutine.h>

namespace controllers {

    /**
     * @brief Manage the persistent access policy and annotate NapCat friends with membership.
     */
    class AccessController : public drogon::HttpController<AccessController> {
    public:
        METHOD_LIST_BEGIN
        ADD_METHOD_TO(AccessController::Access, "/api/admin/access", drogon::Get, drogon::Put);
        ADD_METHOD_TO(AccessController::Allowlist, "/api/admin/allowlist", drogon::Get, drogon::Post);
        ADD_METHOD_TO(AccessController::Blacklist, "/api/admin/blacklist", drogon::Get, drogon::Post);
        ADD_METHOD_TO(AccessController::RemoveAllowlist, "/api/admin/allowlist/{1}", drogon::Delete);
        ADD_METHOD_TO(AccessController::RemoveBlacklist, "/api/admin/blacklist/{1}", drogon::Delete);
        ADD_METHOD_TO(AccessController::Friends, "/api/admin/friends", drogon::Get);
        METHOD_LIST_END

        /**
         * @brief Read or persist the access mode, preserving both lists.
         * @param req GET request or PUT with a string mode field.
         * @return Current or newly saved mode.
         */
        auto Access(drogon::HttpRequestPtr req) const -> drogon::Task<drogon::HttpResponsePtr>;

        /**
         * @brief Read the allowlist or add a QQ number.
         * @param req GET request or POST with a decimal string qq field.
         * @return Current or newly saved allowlist.
         */
        auto Allowlist(drogon::HttpRequestPtr req) const -> drogon::Task<drogon::HttpResponsePtr>;

        /**
         * @brief Read the blacklist or add a QQ number.
         * @param req GET request or POST with a decimal string qq field.
         * @return Current or newly saved blacklist.
         */
        auto Blacklist(drogon::HttpRequestPtr req) const -> drogon::Task<drogon::HttpResponsePtr>;

        /**
         * @brief Idempotently remove a QQ number from the allowlist.
         * @param req Incoming request.
         * @param qq Decimal QQ number from the path.
         * @return Newly saved allowlist.
         */
        auto RemoveAllowlist(
            drogon::HttpRequestPtr req,
            std::string qq
        ) const -> drogon::Task<drogon::HttpResponsePtr>;

        /**
         * @brief Idempotently remove a QQ number from the blacklist.
         * @param req Incoming request.
         * @param qq Decimal QQ number from the path.
         * @return Newly saved blacklist.
         */
        auto RemoveBlacklist(
            drogon::HttpRequestPtr req,
            std::string qq
        ) const -> drogon::Task<drogon::HttpResponsePtr>;

        /**
         * @brief Query NapCat friends and annotate them using one policy snapshot.
         * @param req Incoming request.
         * @return Friend identities, memberships, and policy decisions; 502 on upstream failure.
         */
        auto Friends(drogon::HttpRequestPtr req) const -> drogon::Task<drogon::HttpResponsePtr>;
    };

} // namespace controllers
