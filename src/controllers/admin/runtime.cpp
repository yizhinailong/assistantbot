/**
 * @file runtime.cpp
 * @brief Implement runtime status administration endpoints.
 */
#include "controllers/admin/runtime.hpp"

#include <exception>
#include <string>

#include <drogon/drogon.h>

#include "admin/runtime_state.hpp"

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

    } // namespace

    /**
     * @brief Return the combined runtime status.
     * @param req Incoming request.
     * @return JSON status response.
     */
    auto RuntimeController::Status(
        drogon::HttpRequestPtr /*req*/
    ) const -> drogon::Task<drogon::HttpResponsePtr> {
        try {
            co_return drogon::HttpResponse::newHttpJsonResponse(co_await drogon::app().getPlugin<admin::Runtime>()->Status());
        } catch (std::exception const& error) {
            LOG_ERROR << "Reading administrative status failed: " << error.what();
            co_return json_error("could not read runtime status", drogon::k500InternalServerError);
        }
    }

    /**
     * @brief Return the bot pause flag.
     * @param req Incoming request.
     * @return JSON pause response.
     */
    auto RuntimeController::GetBotStatus(
        drogon::HttpRequestPtr /*req*/
    ) const -> drogon::Task<drogon::HttpResponsePtr> {
        Json::Value body{ Json::objectValue };
        body["paused"] = drogon::app().getPlugin<admin::Runtime>()->Paused();
        co_return drogon::HttpResponse::newHttpJsonResponse(body);
    }

    /**
     * @brief Validate and update the bot pause flag.
     * @param req Incoming JSON request.
     * @return Updated pause response.
     */
    auto RuntimeController::SetBotStatus(
        drogon::HttpRequestPtr req
    ) const -> drogon::Task<drogon::HttpResponsePtr> {
        auto const body{ req->getJsonObject() };
        if (body == nullptr || !body->isObject() || !body->isMember("paused") || !(*body)["paused"].isBool()) {
            co_return json_error("body must be an object with a boolean paused field", drogon::k400BadRequest);
        }
        drogon::app().getPlugin<admin::Runtime>()->SetPaused((*body)["paused"].asBool());
        Json::Value response{ Json::objectValue };
        response["paused"] = drogon::app().getPlugin<admin::Runtime>()->Paused();
        co_return drogon::HttpResponse::newHttpJsonResponse(response);
    }

    /**
     * @brief Return retained process samples.
     * @param req Incoming request.
     * @return JSON samples response.
     */
    auto RuntimeController::ProcessMetrics(
        drogon::HttpRequestPtr /*req*/
    ) const -> drogon::Task<drogon::HttpResponsePtr> {
        co_return drogon::HttpResponse::newHttpJsonResponse(drogon::app().getPlugin<admin::Runtime>()->ProcessMetrics());
    }

} // namespace controllers
