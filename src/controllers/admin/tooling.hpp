/**
 * @file tooling.hpp
 * @brief Declare authenticated Lua tool editing and MCP discovery probes.
 */
#pragma once
#include <drogon/HttpController.h>

namespace controllers {
    /**
     * @brief Manage versioned custom tools and probe configured MCP endpoints.
     */
    class ToolingController : public drogon::HttpController<ToolingController> {
    public:
        METHOD_LIST_BEGIN
        ADD_METHOD_TO(ToolingController::BuiltinList, "/api/admin/tools/builtin", drogon::Get);
        ADD_METHOD_TO(ToolingController::LuaList, "/api/admin/tools/lua", drogon::Get);
        ADD_METHOD_TO(ToolingController::LuaDocument, "/api/admin/tools/lua/{name}", drogon::Get, drogon::Put, drogon::Delete);
        ADD_METHOD_TO(ToolingController::LuaTest, "/api/admin/tools/lua-test", drogon::Post);
        ADD_METHOD_TO(ToolingController::McpTest, "/api/admin/mcp/{name}/test", drogon::Post);
        METHOD_LIST_END

        /**
         * @brief Return the stable built-in schemas for the settings editor.
         * @param req Authenticated request.
         * @return Built-in tool catalog.
         */
        auto BuiltinList(drogon::HttpRequestPtr req) const -> drogon::Task<drogon::HttpResponsePtr>;
        /**
         * @brief List complete persisted Lua documents without running scripts.
         * @param req Authenticated request.
         * @return Versioned tools.
         */
        auto LuaList(drogon::HttpRequestPtr req) const -> drogon::Task<drogon::HttpResponsePtr>;
        /**
         * @brief Read, replace, or remove one revision-guarded Lua tool.
         * @param req Authenticated request with document and expected revision.
         * @param name Tool identifier.
         * @return Saved document or conflict.
         */
        auto LuaDocument(
            drogon::HttpRequestPtr req,
            std::string name
        ) const -> drogon::Task<drogon::HttpResponsePtr>;
        /**
         * @brief Execute an unsaved Lua document using mocked host operations.
         * @param req Tool document and sample arguments.
         * @return Result and simulated host operations.
         */
        auto LuaTest(drogon::HttpRequestPtr req) const -> drogon::Task<drogon::HttpResponsePtr>;
        /**
         * @brief Discover tools on one configured MCP endpoint and close its session.
         * @param req Authenticated request.
         * @param name Configured service name.
         * @return Remote tool definitions, without credentials.
         */
        auto McpTest(
            drogon::HttpRequestPtr req,
            std::string name
        ) const -> drogon::Task<drogon::HttpResponsePtr>;
    };
} // namespace controllers
