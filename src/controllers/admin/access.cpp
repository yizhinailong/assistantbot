/**
 * @file access.cpp
 * @brief Implement authenticated access policy and friend list endpoints.
 */
#include "controllers/admin/access.hpp"

#include <exception>
#include <optional>
#include <stdexcept>
#include <utility>

#include <drogon/drogon.h>

#include <drogon_onebot/onebot_client.hpp>

#include "controllers/onebot/assistant_bot.hpp"
#include "workflow/access_control.hpp"

namespace controllers {

    namespace {

        /**
         * @brief Build a JSON error response.
         * @param message Public error message.
         * @param status HTTP error code.
         * @return JSON response.
         */
        auto json_error(
            std::string const& message,
            drogon::HttpStatusCode status
        ) -> drogon::HttpResponsePtr {
            Json::Value body{ Json::objectValue };
            body["error"] = message;
            auto response{ drogon::HttpResponse::newHttpJsonResponse(body) };
            response->setStatusCode(status);
            return response;
        }

        /**
         * @brief Serialize either the mode or a sorted list of QQ strings.
         * @param policy Policy snapshot.
         * @param list List to return; empty returns the mode.
         * @return JSON response.
         */
        auto policy_response(
            workflow::AccessPolicy const& policy,
            std::optional<workflow::AccessList> list = std::nullopt
        ) -> drogon::HttpResponsePtr {
            Json::Value body{ Json::objectValue };
            if (list) {
                body["qqs"] = Json::Value{ Json::arrayValue };
                auto const& entries{ *list == workflow::AccessList::Allowlist ? policy.allowlist : policy.blacklist };
                for (auto const qq : entries) {
                    body["qqs"].append(std::to_string(qq));
                }
            } else {
                body["mode"] = workflow::access_mode_name(policy.mode);
            }
            return drogon::HttpResponse::newHttpJsonResponse(body);
        }

        /**
         * @brief Read a strict one-field JSON object.
         * @param req Incoming JSON request.
         * @param key Required string field.
         * @return Field value.
         * @throws std::invalid_argument If the body has a wrong shape or field type.
         */
        auto string_field(drogon::HttpRequestPtr const& req, char const* key) -> std::string {
            auto const body{ req->getJsonObject() };
            if (body == nullptr || !body->isObject() || body->size() != 1 || !(*body)[key].isString()) {
                throw std::invalid_argument{ std::string{ "body must be an object containing only the string field " } + key };
            }
            return (*body)[key].asString();
        }

        /**
         * @brief Validate, save, and return a policy change with consistent error handling.
         * @param req Request holding the mode, QQ field, or DELETE method.
         * @param list List to edit; empty edits the mode.
         * @param qq QQ path parameter for DELETE; empty for other methods.
         * @return Saved state, 400 for validation, 409 for contention, or 500 for storage failure.
         */
        auto write_policy(
            drogon::HttpRequestPtr req,
            std::optional<workflow::AccessList> list = std::nullopt,
            std::string qq = {}
        ) -> drogon::Task<drogon::HttpResponsePtr> {
            try {
                auto const access{ drogon::app().getPlugin<AssistantBot>()->Access() };
                std::shared_ptr<workflow::AccessPolicy const> saved{};
                if (list) {
                    auto const removing{ req->method() == drogon::Delete };
                    auto const user_id{ workflow::parse_qq(removing ? qq : string_field(req, "qq")) };
                    saved = co_await access->SetListed(*list, user_id, !removing);
                } else {
                    saved = co_await access->SetMode(workflow::parse_access_mode(string_field(req, "mode")));
                }
                co_return policy_response(*saved, list);
            } catch (std::invalid_argument const& error) {
                co_return json_error(error.what(), drogon::k400BadRequest);
            } catch (workflow::AccessConflict const& error) {
                co_return json_error(error.what(), drogon::k409Conflict);
            } catch (std::exception const& error) {
                LOG_ERROR << "Saving access policy failed: " << error.what();
                co_return json_error("could not save access policy", drogon::k500InternalServerError);
            }
        }

    } // namespace

    /**
     * @brief Read or persist the access mode, preserving both lists.
     * @param req GET request or PUT with a string mode field.
     * @return Current or newly saved mode.
     */
    auto AccessController::Access(
        drogon::HttpRequestPtr req
    ) const -> drogon::Task<drogon::HttpResponsePtr> {
        if (req->method() == drogon::Get) {
            co_return policy_response(*drogon::app().getPlugin<AssistantBot>()->Access()->Current());
        }
        co_return co_await write_policy(std::move(req));
    }

    /**
     * @brief Read the allowlist or add a QQ number.
     * @param req GET request or POST with a decimal string qq field.
     * @return Current or newly saved allowlist.
     */
    auto AccessController::Allowlist(
        drogon::HttpRequestPtr req
    ) const -> drogon::Task<drogon::HttpResponsePtr> {
        if (req->method() == drogon::Get) {
            co_return policy_response(*drogon::app().getPlugin<AssistantBot>()->Access()->Current(), workflow::AccessList::Allowlist);
        }
        co_return co_await write_policy(std::move(req), workflow::AccessList::Allowlist);
    }

    /**
     * @brief Read the blacklist or add a QQ number.
     * @param req GET request or POST with a decimal string qq field.
     * @return Current or newly saved blacklist.
     */
    auto AccessController::Blacklist(
        drogon::HttpRequestPtr req
    ) const -> drogon::Task<drogon::HttpResponsePtr> {
        if (req->method() == drogon::Get) {
            co_return policy_response(*drogon::app().getPlugin<AssistantBot>()->Access()->Current(), workflow::AccessList::Blacklist);
        }
        co_return co_await write_policy(std::move(req), workflow::AccessList::Blacklist);
    }

    /**
     * @brief Idempotently remove a QQ number from the allowlist.
     * @param req Incoming request.
     * @param qq Decimal QQ number from the path.
     * @return Newly saved allowlist.
     */
    auto AccessController::RemoveAllowlist(
        drogon::HttpRequestPtr req,
        std::string qq
    ) const -> drogon::Task<drogon::HttpResponsePtr> {
        co_return co_await write_policy(std::move(req), workflow::AccessList::Allowlist, std::move(qq));
    }

    /**
     * @brief Idempotently remove a QQ number from the blacklist.
     * @param req Incoming request.
     * @param qq Decimal QQ number from the path.
     * @return Newly saved blacklist.
     */
    auto AccessController::RemoveBlacklist(
        drogon::HttpRequestPtr req,
        std::string qq
    ) const -> drogon::Task<drogon::HttpResponsePtr> {
        co_return co_await write_policy(std::move(req), workflow::AccessList::Blacklist, std::move(qq));
    }

    /**
     * @brief Query NapCat friends and annotate them using one policy snapshot.
     * @param req Incoming request.
     * @return Friend identities, memberships, and policy decisions; 502 on upstream failure.
     */
    auto AccessController::Friends(
        drogon::HttpRequestPtr /*req*/
    ) const -> drogon::Task<drogon::HttpResponsePtr> {
        try {
            auto const client{ drogon::app().getPlugin<drogon_onebot::OneBotClient>()->NewOneBotClient() };
            auto const friends{ co_await client.GetFriendList({ .timeout = 2.0 }) };
            if (!friends) {
                co_return json_error("could not read friends from NapCat", drogon::k502BadGateway);
            }
            auto const policy{ drogon::app().getPlugin<AssistantBot>()->Access()->Current() };
            Json::Value body{ Json::objectValue };
            body["friends"] = Json::Value{ Json::arrayValue };
            for (auto const& info : *friends) {
                Json::Value friend_info{ Json::objectValue };
                friend_info["user_id"] = std::to_string(info.user_id);
                friend_info["nickname"] = info.nickname;
                friend_info["remark"] = info.remark;
                friend_info["in_allowlist"] = policy->allowlist.contains(info.user_id);
                friend_info["in_blacklist"] = policy->blacklist.contains(info.user_id);
                friend_info["allowed"] = policy->Allows(info.user_id);
                body["friends"].append(std::move(friend_info));
            }
            co_return drogon::HttpResponse::newHttpJsonResponse(body);
        } catch (std::exception const& error) {
            LOG_ERROR << "Reading NapCat friends failed: " << error.what();
            co_return json_error("could not read friends from NapCat", drogon::k502BadGateway);
        }
    }

} // namespace controllers
