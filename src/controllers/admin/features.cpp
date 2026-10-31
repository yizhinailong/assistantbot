/**
 * @file features.cpp
 * @brief Persist feature forms and merge typed group membership with saved configuration.
 */
#include "controllers/admin/features.hpp"

#include "admin/http_helpers.hpp"
#include "admin/http_traces.hpp"
#include "admin/live_events.hpp"
#include "controllers/onebot/assistant_bot.hpp"
#include "workflow/access_control.hpp"
#include "workflow/feature_settings.hpp"

namespace {
    /**
     * @brief Read an optimistic revision without precision loss in browser JSON.
     * @param body JSON request object.
     * @return Optional expected revision.
     */
    auto revision(Json::Value const& body) -> std::optional<std::uint64_t> {
        if (!body.isObject() || !body.isMember("values")) {
            throw std::invalid_argument{ "expected an object with values" };
        }
        for (auto const& key : body.getMemberNames()) {
            if (key != "values" && key != "revision") {
                throw std::invalid_argument{ "unknown request field: " + key };
            }
        }
        if (!body.isMember("revision")) {
            return {};
        }
        if (!body["revision"].isString()) {
            throw std::invalid_argument{ "revision must be a decimal string" };
        }
        return static_cast<std::uint64_t>(admin::decimal(body["revision"].asString()));
    }
} // namespace

namespace controllers {
    /**
     * @brief Read or atomically replace a validated optional-feature section.
     * @param req Authenticated request with values and optional decimal revision.
     * @param section Known feature section name.
     * @return Section with defaults and its revision, or an HTTP error.
     */
    auto FeaturesController::Section(
        drogon::HttpRequestPtr req,
        std::string section
    ) const -> drogon::Task<drogon::HttpResponsePtr> {
        try {
            auto const settings{ drogon::app().getPlugin<AssistantBot>()->Features() };
            if (req->method() == drogon::Put) {
                auto const body{ req->getJsonObject() };
                if (!body) {
                    throw std::invalid_argument{ "expected JSON request" };
                }
                auto const expected{ revision(*body) };
                co_await settings->Update(section, (*body)["values"], expected);
                Json::Value event{};
                event["type"] = "features_updated";
                event["section"] = section;
                admin::publish_event(std::move(event));
            }
            co_return drogon::HttpResponse::newHttpJsonResponse(settings->Describe(section));
        } catch (std::invalid_argument const& error) {
            co_return admin::json_error(error.what(), drogon::k400BadRequest);
        } catch (drogon::orm::DrogonDbException const& error) {
            LOG_ERROR << "Saving features failed: " << error.base().what();
            co_return admin::json_error("could not save feature settings", drogon::k500InternalServerError);
        } catch (std::runtime_error const& error) {
            co_return admin::json_error(error.what(), drogon::k409Conflict);
        }
    }

    /**
     * @brief Merge typed OneBot group membership with saved opt-in settings.
     * @param req Authenticated request.
     * @return Groups, their configuration, and the current revision.
     */
    auto FeaturesController::Groups(
        drogon::HttpRequestPtr
    ) const -> drogon::Task<drogon::HttpResponsePtr> {
        auto const groups{ co_await admin::traced_onebot().GetGroupList({ .timeout = 5.0 }) };
        if (!groups) {
            co_return admin::json_error(groups.error().message, drogon::k502BadGateway);
        }
        auto const settings{ drogon::app().getPlugin<AssistantBot>()->Features()->Describe("groups") };
        Json::Value body{};
        body["revision"] = settings["revision"];
        body["groups"] = Json::Value{ Json::arrayValue };
        for (auto const& group : *groups) {
            Json::Value item{};
            auto const id{ std::to_string(group.group_id) };
            item["group_id"] = id;
            item["name"] = group.group_name;
            item["member_count"] = group.member_count;
            item["values"] = settings["values"].get(id, Json::Value{});
            if (item["values"].isNull()) {
                item["values"]["name"] = group.group_name;
                item["values"]["enabled"] = false;
                item["values"]["auto_reply"] = false;
                item["values"]["include_card"] = true;
                item["values"]["commands_enabled"] = true;
                item["values"]["additional_prompt"] = "";
            }
            body["groups"].append(std::move(item));
        }
        co_return drogon::HttpResponse::newHttpJsonResponse(body);
    }

    /**
     * @brief Save one group's complete configuration without dropping other groups.
     * @param req Authenticated request with values and optional revision.
     * @param qq Positive group QQ number.
     * @return Saved groups section or an HTTP error.
     */
    auto FeaturesController::Group(
        drogon::HttpRequestPtr req,
        std::string qq
    ) const -> drogon::Task<drogon::HttpResponsePtr> {
        try {
            auto const id{ std::to_string(workflow::parse_qq(qq)) };
            auto const body{ req->getJsonObject() };
            if (!body) {
                throw std::invalid_argument{ "expected JSON request" };
            }
            auto const settings{ drogon::app().getPlugin<AssistantBot>()->Features() };
            auto const current{ settings->Current() };
            auto expected{ revision(*body) };
            if (!expected) {
                expected = (*current)["_revision"].asUInt64();
            }
            auto groups{ (*current)["groups"] };
            groups[id] = (*body)["values"];
            co_await settings->Update("groups", std::move(groups), expected);
            co_return drogon::HttpResponse::newHttpJsonResponse(settings->Describe("groups"));
        } catch (std::invalid_argument const& error) {
            co_return admin::json_error(error.what(), drogon::k400BadRequest);
        } catch (drogon::orm::DrogonDbException const& error) {
            LOG_ERROR << "Saving group failed: " << error.base().what();
            co_return admin::json_error("could not save group", drogon::k500InternalServerError);
        } catch (std::runtime_error const& error) {
            co_return admin::json_error(error.what(), drogon::k409Conflict);
        }
    }
} // namespace controllers
