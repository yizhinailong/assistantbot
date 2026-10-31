/**
 * @file lua.hpp
 * @brief Declare isolated Lua custom tools with bounded computation and yielding host operations.
 */
#pragma once
#include <functional>
#include <string>

#include <drogon/utils/coroutine.h>
#include <json/value.h>

namespace tools {
    using LuaHost = std::function<drogon::Task<Json::Value>(Json::Value)>; ///< Asynchronous current-conversation host operation.
    /**
     * @brief Check custom tool metadata, a supported JSON schema, and Lua syntax.
     * @param name Tool identifier.
     * @param value Complete custom tool document.
     * @return Normalized document with defaults.
     * @throws std::invalid_argument If metadata, schema, or syntax is invalid.
     */
    auto validate_lua_tool(std::string const& name, Json::Value value) -> Json::Value;
    /**
     * @brief Validate arguments against the supported JSON Schema subset.
     * @param schema Parameter schema.
     * @param value Model-supplied argument value.
     * @throws std::invalid_argument For missing, unexpected, or mistyped arguments.
     */
    auto validate_arguments(Json::Value const& schema, Json::Value const& value) -> void;
    /**
     * @brief Execute run(args, ctx) in a fresh restricted Lua state.
     * @param script At most 256 KiB of Lua source.
     * @param args Validated JSON arguments.
     * @param context Owned sender/conversation metadata.
     * @param host Optional asynchronous host callback; admin tests provide mocks.
     * @return JSON result; throws on limits, invalid results, or script errors.
     */
    auto run_lua(
        std::string script,
        Json::Value args,
        Json::Value context,
        LuaHost host = {}
    ) -> drogon::Task<Json::Value>;
} // namespace tools
