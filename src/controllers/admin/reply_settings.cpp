/**
 * @file reply_settings.cpp
 * @brief Read and persist validated reply settings without exposing provider secrets.
 */
#include "controllers/admin/reply_settings.hpp"

#include <drogon_openai/openai_client.hpp>

#include "admin/http_helpers.hpp"
#include "admin/live_events.hpp"
#include "controllers/onebot/assistant_bot.hpp"
#include "workflow/bot_settings.hpp"

namespace controllers {

    /**
     * @brief Read effective reply settings or persist a validated override object.
     * @param req Authenticated administrative request.
     * @return JSON result or a structured HTTP error.
     */
    auto ReplySettingsController::Settings(
        drogon::HttpRequestPtr req
    ) const -> drogon::Task<drogon::HttpResponsePtr> {
        auto const settings{ drogon::app().getPlugin<AssistantBot>()->Settings() };
        try {
            if (req->method() == drogon::Put) {
                auto const body{ req->getJsonObject() };
                if (body == nullptr) {
                    throw std::invalid_argument{ "controllers::AssistantBot: settings must be an object" };
                }
                co_await settings->Update(*body);
                Json::Value event{ Json::objectValue };
                event["type"] = "settings_updated";
                admin::publish_event(std::move(event));
            }
            co_return drogon::HttpResponse::newHttpJsonResponse(settings->Describe(drogon::app().getPlugin<drogon_openai::OpenAIClient>()->Config().model));
        } catch (std::invalid_argument const& error) {
            co_return admin::json_error(error.what(), drogon::k400BadRequest);
        } catch (drogon::orm::DrogonDbException const& error) {
            LOG_ERROR << "Saving reply settings failed: " << error.base().what();
            co_return admin::json_error("could not save reply settings", drogon::k500InternalServerError);
        } catch (std::runtime_error const& error) {
            co_return admin::json_error(error.what(), drogon::k409Conflict);
        }
    }

} // namespace controllers
