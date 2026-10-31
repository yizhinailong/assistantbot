/**
 * @file tooling.cpp
 * @brief Validate custom tool documents, simulate Lua hosts, and probe MCP discovery.
 */
#include "controllers/admin/tooling.hpp"

#include "admin/http_helpers.hpp"
#include "controllers/onebot/assistant_bot.hpp"
#include "storage/database.hpp"
#include "storage/feature_records.hpp"
#include "tools/catalog.hpp"
#include "tools/lua.hpp"
#include "tools/mcp.hpp"
#include "workflow/feature_settings.hpp"

namespace controllers {
    /**
     * @brief Return the stable built-in schemas for the settings editor.
     * @param req Authenticated request.
     * @return Built-in tool catalog.
     */
    auto ToolingController::BuiltinList(
        drogon::HttpRequestPtr
    ) const -> drogon::Task<drogon::HttpResponsePtr> {
        Json::Value result{};
        result["tools"] = tools::builtin_catalog();
        co_return drogon::HttpResponse::newHttpJsonResponse(result);
    }

    /**
     * @brief List complete persisted Lua documents without running scripts.
     * @param req Authenticated request.
     * @return Versioned tools.
     */
    auto ToolingController::LuaList(
        drogon::HttpRequestPtr
    ) const -> drogon::Task<drogon::HttpResponsePtr> {
        try {
            Json::Value result{};
            result["tools"] = co_await storage::feature_records(drogon::app().getPlugin<storage::Database>()->Client(), "lua");
            co_return drogon::HttpResponse::newHttpJsonResponse(result);
        } catch (std::exception const&) {
            co_return admin::json_error("could not read tools", drogon::k500InternalServerError);
        }
    }

    /**
     * @brief Read, replace, or remove one revision-guarded Lua tool.
     * @param req Authenticated request with document and expected revision.
     * @param name Tool identifier.
     * @return Saved document or conflict.
     */
    auto ToolingController::LuaDocument(
        drogon::HttpRequestPtr req,
        std::string name
    ) const -> drogon::Task<drogon::HttpResponsePtr> {
        try {
            auto const db{ drogon::app().getPlugin<storage::Database>()->Client() };
            if (req->method() == drogon::Get) {
                auto const records{ co_await storage::feature_records(db, "lua", name) };
                if (records.empty()) {
                    co_return admin::json_error("tool not found", drogon::k404NotFound);
                }
                co_return drogon::HttpResponse::newHttpJsonResponse(records[0]);
            }
            auto const body{ req->getJsonObject() };
            auto const revision{ req->method() == drogon::Delete ? admin::decimal(req->getParameter("revision"), 1) : body && (*body)["revision"].isString() ? admin::decimal((*body)["revision"].asString(), 0) :
                                                                                                                                                               throw std::invalid_argument{ "revision is required" } };
            if (req->method() == drogon::Delete) {
                if (!(co_await storage::delete_feature_record(db, "lua", name, revision))) {
                    co_return admin::json_error("tool changed; refresh before deleting", drogon::k409Conflict);
                }
                Json::Value result{};
                result["deleted"] = true;
                co_return drogon::HttpResponse::newHttpJsonResponse(result);
            }
            auto value{ tools::validate_lua_tool(name, (*body)["value"]) };
            if (revision == 0 && (co_await storage::feature_records(db, "lua")).size() >= 100) {
                throw std::invalid_argument{ "at most 100 Lua tools are allowed" };
            }
            co_return drogon::HttpResponse::newHttpJsonResponse(co_await storage::save_feature_record(db, "lua", name, std::move(value), revision));
        } catch (std::invalid_argument const& error) {
            co_return admin::json_error(error.what(), drogon::k400BadRequest);
        } catch (drogon::orm::DrogonDbException const&) {
            co_return admin::json_error("tool storage failed", drogon::k500InternalServerError);
        } catch (std::runtime_error const& error) {
            co_return admin::json_error(error.what(), drogon::k409Conflict);
        }
    }

    /**
     * @brief Execute an unsaved Lua document using mocked host operations.
     * @param req Tool document and sample arguments.
     * @return Result and simulated host operations.
     */
    auto ToolingController::LuaTest(
        drogon::HttpRequestPtr req
    ) const -> drogon::Task<drogon::HttpResponsePtr> {
        try {
            auto const body{ req->getJsonObject() };
            if (!body || !(*body)["name"].isString() || !(*body)["arguments"].isObject()) {
                throw std::invalid_argument{ "name, value, and arguments are required" };
            }
            auto const value{ tools::validate_lua_tool((*body)["name"].asString(), (*body)["value"]) };
            tools::validate_arguments(value["parameters"], (*body)["arguments"]);
            Json::Value result{};
            result["operations"] = Json::Value{ Json::arrayValue };
            Json::Value context{};
            context["session_id"] = "test";
            context["is_private"] = true;
            result["result"] = co_await tools::run_lua(value["script"].asString(), (*body)["arguments"], context, [&result](Json::Value operation) -> drogon::Task<Json::Value> {
                result["operations"].append(operation);
                Json::Value response{};
                response["ok"] = true;
                response["mock"] = true;
                response["status"] = 200;
                response["body"] = operation.get("body", "");
                response["message_id"] = "mock-message";
                response["task_id"] = "mock-task";
                co_return response;
            });
            co_return drogon::HttpResponse::newHttpJsonResponse(result);
        } catch (std::exception const& error) {
            co_return admin::json_error(error.what(), drogon::k400BadRequest);
        }
    }

    /**
     * @brief Discover tools on one configured MCP endpoint and close its session.
     * @param req Authenticated request.
     * @param name Configured service name.
     * @return Remote tool definitions, without credentials.
     */
    auto ToolingController::McpTest(
        drogon::HttpRequestPtr,
        std::string name
    ) const -> drogon::Task<drogon::HttpResponsePtr> {
        try {
            auto const snapshot{ drogon::app().getPlugin<AssistantBot>()->Features()->Current() };
            for (auto const& server : (*snapshot)["mcp"]["servers"]) {
                if (server["name"].asString() != name) {
                    continue;
                }
                tools::McpSession session{ server };
                Json::Value result{};
                std::string error{};
                try {
                    result["tools"] = co_await session.List();
                } catch (std::exception const& exception) {
                    error = exception.what();
                }
                co_await session.Close();
                if (!error.empty()) {
                    co_return admin::json_error(error, drogon::k502BadGateway);
                }
                co_return drogon::HttpResponse::newHttpJsonResponse(result);
            }
            co_return admin::json_error("MCP service not found", drogon::k404NotFound);
        } catch (std::exception const& error) {
            co_return admin::json_error(error.what(), drogon::k400BadRequest);
        }
    }
} // namespace controllers
