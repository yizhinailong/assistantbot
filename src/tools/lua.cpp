/**
 * @file lua.cpp
 * @brief Bound Lua memory, instructions, CPU, JSON conversion, and yielding host operations.
 */
#include "tools/lua.hpp"

#include <chrono>
#include <cmath>
#include <cstdlib>
#include <memory>
#include <regex>
#include <set>
#include <stdexcept>

#include <drogon/utils/Utilities.h>
#include <lauxlib.h>
#include <lua.h>
#include <lualib.h>

#include "common/json.hpp"
#include "tools/catalog.hpp"

namespace {
    char JSON_NULL{}; ///< Opaque Lua representation of JSON null.

    /**
     * @brief Execution limits shared by a Lua state and its coroutine.
     */
    struct Budget {
        std::size_t bytes{};                                                               ///< Currently allocated Lua bytes.
        std::size_t instructions{};                                                        ///< Instructions sampled every thousand steps.
        std::chrono::steady_clock::time_point resumed{ std::chrono::steady_clock::now() }; ///< Start of active computation.
        std::chrono::steady_clock::duration spent{};                                       ///< Prior active computation excluding host waits.
    };

    /**
     * @brief Enforce the 16 MiB Lua allocation cap without throwing through the allocator.
     * @param userdata Budget shared with the interpreter.
     * @param pointer Previous allocation, or null.
     * @param old_size Previous allocation size or a Lua type tag when pointer is null.
     * @param size Requested allocation size, or zero to free.
     * @return Resized allocation or null when the cap or allocation fails.
     */
    auto allocate(void* userdata, void* pointer, std::size_t old_size, std::size_t size) -> void* {
        auto& budget{ *static_cast<Budget*>(userdata) };
        auto const old{ pointer ? old_size : 0 };
        if (size == 0) {
            budget.bytes -= old;
            std::free(pointer);
            return nullptr;
        }
        if (size > 16 * 1024 * 1024 || budget.bytes - old > 16 * 1024 * 1024 - size) {
            return nullptr;
        }
        auto* result{ std::realloc(pointer, size) };
        if (result) {
            budget.bytes = budget.bytes - old + size;
        }
        return result;
    }

    /**
     * @brief Interrupt Lua after one million instructions or 100 ms of active execution.
     * @param state Active coroutine.
     * @param debug Unused hook metadata.
     */
    auto instruction_hook(lua_State* state, lua_Debug* debug) -> void {
        static_cast<void>(debug);
        auto& budget{ **static_cast<Budget**>(lua_getextraspace(state)) };
        budget.instructions += 1000;
        if (budget.instructions > 1000000 || budget.spent + std::chrono::steady_clock::now() - budget.resumed > std::chrono::milliseconds{ 100 }) {
            luaL_error(state, "Lua execution budget exceeded");
        }
    }

    /**
     * @brief Convert one Lua value to bounded JSON, rejecting cycles through the depth bound.
     * @param state Interpreter stack.
     * @param index Stack value to read.
     * @param depth Current nesting depth.
     * @param nodes Total visited JSON nodes.
     * @param bytes Total copied string and key bytes.
     * @return JSON value.
     */
    auto from_lua_value(
        lua_State* state,
        int index,
        int depth,
        std::size_t& nodes,
        std::size_t& bytes
    ) -> Json::Value {
        if (++nodes > 20000 || bytes > 1024 * 1024) {
            throw std::invalid_argument{ "Lua JSON conversion budget exceeded" };
        }
        if (depth > 32) {
            throw std::invalid_argument{ "Lua result exceeds 32 JSON nesting levels" };
        }
        if (!lua_checkstack(state, 4)) {
            throw std::runtime_error{ "Lua JSON stack budget exceeded" };
        }
        index = lua_absindex(state, index);
        switch (lua_type(state, index)) {
            case LUA_TLIGHTUSERDATA:
                if (lua_touserdata(state, index) == &JSON_NULL) {
                    return {};
                }
                throw std::invalid_argument{ "unknown Lua userdata" };
            case LUA_TNIL    : return {};
            case LUA_TBOOLEAN: return lua_toboolean(state, index) != 0;
            case LUA_TNUMBER:
                if (lua_isinteger(state, index)) {
                    return Json::Int64{ lua_tointeger(state, index) };
                }
                if (!std::isfinite(lua_tonumber(state, index))) {
                    throw std::invalid_argument{ "Lua JSON numbers must be finite" };
                }
                return lua_tonumber(state, index);
            case LUA_TSTRING: {
                std::size_t size{};
                auto const* text{ lua_tolstring(state, index, &size) };
                bytes += size;
                if (bytes > 1024 * 1024) {
                    throw std::invalid_argument{ "Lua result strings exceed 1 MiB" };
                }
                return std::string{ text, size };
            }
            case LUA_TTABLE: {
                std::size_t count{};
                bool array{ true };
                auto const length{ lua_rawlen(state, index) };
                lua_pushnil(state);
                while (lua_next(state, index)) {
                    ++count;
                    if (count > 10000) {
                        throw std::invalid_argument{ "Lua table exceeds 10000 entries" };
                    }
                    array = array && lua_isinteger(state, -2) && lua_tointeger(state, -2) > 0 && static_cast<lua_Unsigned>(lua_tointeger(state, -2)) <= length;
                    lua_pop(state, 1);
                }
                Json::Value value{ array && count > 0 && count == length ? Json::arrayValue : Json::objectValue };
                if (value.isArray()) {
                    for (std::size_t i{ 1 }; i <= length; ++i) {
                        lua_geti(state, index, static_cast<lua_Integer>(i));
                        value.append(from_lua_value(state, -1, depth + 1, nodes, bytes));
                        lua_pop(state, 1);
                    }
                } else {
                    lua_pushnil(state);
                    while (lua_next(state, index)) {
                        if (lua_type(state, -2) != LUA_TSTRING) {
                            throw std::invalid_argument{ "Lua JSON object keys must be strings" };
                        }
                        std::size_t size{};
                        auto const* key{ lua_tolstring(state, -2, &size) };
                        bytes += size;
                        if (bytes > 1024 * 1024) {
                            throw std::invalid_argument{ "Lua JSON keys exceed 1 MiB" };
                        }
                        value[std::string{ key, size }] = from_lua_value(state, -1, depth + 1, nodes, bytes);
                        lua_pop(state, 1);
                    }
                }
                return value;
            }
            default: throw std::invalid_argument{ "Lua results must contain only JSON-compatible values" };
        }
    }

    /**
     * @brief Convert a result with a total node and string budget, including aliased tables.
     * @param state Interpreter stack.
     * @param index Stack value to read.
     * @return Bounded JSON value.
     */
    auto from_lua(lua_State* state, int index) -> Json::Value {
        std::size_t nodes{}, bytes{};
        return from_lua_value(state, index, 0, nodes, bytes);
    }

    /**
     * @brief Push JSON onto the Lua stack with a shared depth bound.
     * @param state Interpreter stack.
     * @param value Value to convert.
     * @param depth Current nesting depth.
     */
    auto to_lua(lua_State* state, Json::Value const& value, int depth = 0) -> void {
        if (!lua_checkstack(state, 4)) {
            throw std::runtime_error{ "Lua input stack budget exceeded" };
        }
        if (depth > 32) {
            throw std::invalid_argument{ "Lua input exceeds 32 nesting levels" };
        }
        if (value.isNull()) {
            lua_pushlightuserdata(state, &JSON_NULL);
        } else if (value.isBool()) {
            lua_pushboolean(state, value.asBool());
        } else if (value.isInt64()) {
            lua_pushinteger(state, value.asInt64());
        } else if (value.isNumeric()) {
            lua_pushnumber(state, value.asDouble());
        } else if (value.isString()) {
            auto const text{ value.asString() };
            lua_pushlstring(state, text.data(), text.size());
        } else if (value.isArray()) {
            lua_createtable(state, static_cast<int>(value.size()), 0);
            for (Json::ArrayIndex i{}; i < value.size(); ++i) {
                to_lua(state, value[i], depth + 1);
                lua_seti(state, -2, i + 1);
            }
        } else {
            lua_createtable(state, 0, static_cast<int>(value.size()));
            for (auto const& key : value.getMemberNames()) {
                to_lua(state, value[key], depth + 1);
                lua_setfield(state, -2, key.c_str());
            }
        }
    }

    /**
     * @brief Push a host result inside a protected Lua call so memory exhaustion cannot panic.
     * @param state Protected call stack containing a borrowed JSON pointer.
     * @return One converted Lua value.
     */
    auto push_json(lua_State* state) -> int {
        auto const* value{ static_cast<Json::Value const*>(lua_touserdata(state, 1)) };
        to_lua(state, *value);
        return 1;
    }

    /**
     * @brief Convert host data while preserving Lua allocation error handling.
     * @param state Interpreter stack with spare call slots.
     * @param value JSON data that remains owned through the protected call.
     */
    auto protected_push(lua_State* state, Json::Value const& value) -> void {
        if (common::json_text(value).size() > 1024 * 1024) {
            throw std::invalid_argument{ "Lua host input exceeds 1 MiB" };
        }
        lua_pushcfunction(state, push_json);
        lua_pushlightuserdata(state, const_cast<Json::Value*>(&value));
        if (lua_pcall(state, 1, 1, 0) != LUA_OK) {
            throw std::runtime_error{ "Lua host result exceeds interpreter limits" };
        }
    }

    /**
     * @brief Implement json.encode without allowing arbitrary Lua values.
     * @param state Lua call stack.
     * @return One encoded JSON string.
     */
    auto encode(lua_State* state) -> int {
        try {
            auto const text{ common::json_text(from_lua(state, 1)) };
            if (text.size() > 1024 * 1024) {
                throw std::invalid_argument{ "JSON exceeds 1 MiB" };
            }
            lua_pushlstring(state, text.data(), text.size());
            return 1;
        } catch (std::exception const& error) {
            return luaL_error(state, "%s", error.what());
        }
    }

    /**
     * @brief Implement json.decode using the shared bounded parser.
     * @param state Lua call stack.
     * @return One decoded JSON value.
     */
    auto decode(lua_State* state) -> int {
        try {
            std::size_t size{};
            auto const* text{ luaL_checklstring(state, 1, &size) };
            if (size > 1024 * 1024) {
                throw std::invalid_argument{ "JSON exceeds 1 MiB" };
            }
            to_lua(state, common::parse_json(std::string{ text, size }));
            return 1;
        } catch (std::exception const& error) {
            return luaL_error(state, "%s", error.what());
        }
    }

    /**
     * @brief Percent-encode a bounded UTF-8 string for URL query parameters.
     * @param state Lua call stack.
     * @return One encoded string.
     */
    auto url_encode(lua_State* state) -> int {
        std::size_t size{};
        auto const* text{ luaL_checklstring(state, 1, &size) };
        if (size > 1024 * 1024) {
            return luaL_error(state, "URL input exceeds 1 MiB");
        }
        auto value{ drogon::utils::urlEncode(std::string{ text, size }) };
        lua_pushlstring(state, value.data(), value.size());
        return 1;
    }

    /**
     * @brief Create an isolated Lua state with only selected safe standard libraries.
     * @param budget Limits that outlive the state.
     * @return Interpreter whose allocation errors unwind C++ objects through lua-cpp.
     */
    auto new_state(Budget& budget) -> std::unique_ptr<lua_State, decltype(&lua_close)> {
#if LUA_VERSION_NUM >= 505
        auto* state{ lua_newstate(allocate, &budget, 0) };
#else
        auto* state{ lua_newstate(allocate, &budget) };
#endif
        if (!state) {
            throw std::runtime_error{ "could not allocate Lua state" };
        }
        std::unique_ptr<lua_State, decltype(&lua_close)> owned{ state, lua_close };
        *static_cast<Budget**>(lua_getextraspace(state)) = &budget;
        for (auto const& [name, open] : { std::pair{ LUA_GNAME, luaopen_base }, std::pair{ LUA_TABLIBNAME, luaopen_table }, std::pair{ LUA_STRLIBNAME, luaopen_string }, std::pair{ LUA_MATHLIBNAME, luaopen_math }, std::pair{ LUA_UTF8LIBNAME, luaopen_utf8 }, std::pair{ LUA_COLIBNAME, luaopen_coroutine } }) {
            luaL_requiref(state, name, open, 1);
            lua_pop(state, 1);
        }
        for (auto const* name : { "load", "loadfile", "dofile", "collectgarbage", "pcall", "xpcall", "print", "warn" }) {
            lua_pushnil(state);
            lua_setglobal(state, name);
        }
        lua_getglobal(state, "string");
        for (auto const* name : { "dump", "match", "gmatch", "gsub" }) {
            lua_pushnil(state);
            lua_setfield(state, -2, name);
        }
        lua_pop(state, 1);
        lua_newtable(state);
        lua_pushcfunction(state, encode);
        lua_setfield(state, -2, "encode");
        lua_pushcfunction(state, decode);
        lua_setfield(state, -2, "decode");
        lua_pushlightuserdata(state, &JSON_NULL);
        lua_setfield(state, -2, "null");
        lua_setglobal(state, "json");
        lua_newtable(state);
        lua_pushcfunction(state, url_encode);
        lua_setfield(state, -2, "encode");
        lua_setglobal(state, "url");
        constexpr char const* BOOTSTRAP{ R"lua(
local yield_host = coroutine.yield
local plain_find = string.find
string.find = function(s, pattern, start) return plain_find(s, pattern, start, true) end
http = {}
http.request = function(options) options.operation = 'http'; return yield_host(options) end
http.get = function(url) return http.request({url=url, method='GET'}) end
http.post = function(url, body) return http.request({url=url, method='POST', body=body}) end
bot = {}
bot.send_message = function(text) return yield_host({operation='send_message',text=text}) end
bot.schedule = function(due_at,text,daily) return yield_host({operation='schedule',due_at=due_at,text=text,daily=daily or false}) end
coroutine = nil
)lua" };
        if (luaL_loadbuffer(state, BOOTSTRAP, std::char_traits<char>::length(BOOTSTRAP), "bootstrap") != LUA_OK || lua_pcall(state, 0, 0, 0) != LUA_OK) {
            throw std::runtime_error{ "could not initialize Lua sandbox" };
        }
        lua_sethook(state, instruction_hook, LUA_MASKCOUNT, 1000);
        return owned;
    }

    /**
     * @brief Validate supported schema structure or one argument value recursively.
     * @param schema JSON Schema subset.
     * @param value Optional argument value; null pointer validates schema only.
     * @param depth Nesting depth.
     */
    auto schema_check(Json::Value const& schema, Json::Value const* value, int depth = 0) -> void {
        if (depth > 16 || !schema.isObject()) {
            throw std::invalid_argument{ "tool schemas must be objects with at most 16 nesting levels" };
        }
        static std::set<std::string> const KEYS{ "type", "description", "properties", "required", "additionalProperties", "items", "enum", "minimum", "maximum", "minLength", "maxLength", "minItems", "maxItems", "default", "title" };
        for (auto const& key : schema.getMemberNames()) {
            if (!KEYS.contains(key)) {
                throw std::invalid_argument{ "unsupported Lua parameter schema keyword: " + key };
            }
        }
        if (schema.isMember("type") && !schema["type"].isString()) {
            throw std::invalid_argument{ "schema type must be a string" };
        }
        auto const type{ schema.get("type", "object").asString() };
        if (type != "object" && type != "array" && type != "string" && type != "integer" && type != "number" && type != "boolean" && type != "null") {
            throw std::invalid_argument{ "unsupported parameter type" };
        }
        if (schema.isMember("additionalProperties") && !schema["additionalProperties"].isBool()) {
            throw std::invalid_argument{ "additionalProperties must be boolean" };
        }
        if (schema.isMember("required") && !schema["required"].isArray()) {
            throw std::invalid_argument{ "required must be an array" };
        }
        if (schema.isMember("enum") && (!schema["enum"].isArray() || schema["enum"].empty())) {
            throw std::invalid_argument{ "enum must be a nonempty array" };
        }
        for (auto const* key : { "minimum", "maximum", "minLength", "maxLength", "minItems", "maxItems" }) {
            if (schema.isMember(key) && (!schema[key].isNumeric() || !std::isfinite(schema[key].asDouble()))) {
                throw std::invalid_argument{ "schema limits must be finite numbers" };
            }
        }
        if (value) {
            bool const valid{ type == "object" ? value->isObject() : type == "array" ? value->isArray() :
                                  type == "string"                                   ? value->isString() :
                                  type == "integer"                                  ? value->isInt64() :
                                  type == "number"                                   ? value->isNumeric() :
                                  type == "boolean"                                  ? value->isBool() :
                                                                                       value->isNull() };
            if (!valid) {
                throw std::invalid_argument{ "tool argument has the wrong type: " + type };
            }
            if (schema["enum"].isArray()) {
                bool found{};
                for (auto const& entry : schema["enum"]) {
                    found = found || entry == *value;
                }
                if (!found) {
                    throw std::invalid_argument{ "tool argument is not in enum" };
                }
            }
            if (value->isNumeric() && ((schema.isMember("minimum") && value->asDouble() < schema["minimum"].asDouble()) || (schema.isMember("maximum") && value->asDouble() > schema["maximum"].asDouble()))) {
                throw std::invalid_argument{ "tool argument number is outside the permitted range" };
            }
            auto length{ value->isString() ? value->asString().size() : (value->isArray() ? value->size() : 0) };
            for (auto const& [key, lower] : { std::pair{ type == "string" ? "minLength" : "minItems", true }, std::pair{ type == "string" ? "maxLength" : "maxItems", false } }) {
                if ((value->isString() || value->isArray()) && schema.isMember(key) && (lower ? length < schema[key].asDouble() : length > schema[key].asDouble())) {
                    throw std::invalid_argument{ "tool argument length is outside the permitted range" };
                }
            }
        }
        if (type == "object") {
            if (schema.isMember("properties") && !schema["properties"].isObject()) {
                throw std::invalid_argument{ "properties must be an object" };
            }
            if (schema["required"].isArray()) {
                for (auto const& key : schema["required"]) {
                    if (!key.isString() || !schema["properties"].isMember(key.asString())) {
                        throw std::invalid_argument{ "required names must exist in properties" };
                    }
                    if (value && !value->isMember(key.asString())) {
                        throw std::invalid_argument{ "required tool argument is missing: " + key.asString() };
                    }
                }
            }
            if (schema["properties"].isObject()) {
                for (auto const& key : schema["properties"].getMemberNames()) {
                    schema_check(schema["properties"][key], value && value->isMember(key) ? &(*value)[key] : nullptr, depth + 1);
                }
            }
            if (value && !schema.get("additionalProperties", false).asBool()) {
                for (auto const& key : value->getMemberNames()) {
                    if (!schema["properties"].isMember(key)) {
                        throw std::invalid_argument{ "unexpected tool argument: " + key };
                    }
                }
            }
        }
        if (type == "array") {
            if (!schema["items"].isObject()) {
                throw std::invalid_argument{ "array schemas require items" };
            }
            schema_check(schema["items"], nullptr, depth + 1);
            if (value) {
                for (auto const& entry : *value) {
                    schema_check(schema["items"], &entry, depth + 1);
                }
            }
        }
    }
} // namespace

namespace tools {
    /**
     * @brief Check custom tool metadata, a supported JSON schema, and Lua syntax.
     * @param name Tool identifier.
     * @param value Complete custom tool document.
     * @return Normalized document with defaults.
     * @throws std::invalid_argument If metadata, schema, or syntax is invalid.
     */
    auto validate_lua_tool(std::string const& name, Json::Value value) -> Json::Value {
        static std::regex const NAME{ "[A-Za-z][A-Za-z0-9_]{0,47}" };
        if (!std::regex_match(name, NAME) || name.starts_with("mcp_") || name.starts_with("builtin_") || is_builtin(name)) {
            throw std::invalid_argument{ "Lua tool name must contain 1..48 ASCII letters, digits, or underscores" };
        }
        if (!value.isObject()) {
            throw std::invalid_argument{ "Lua tool must be an object" };
        }
        for (auto const& key : value.getMemberNames()) {
            if (key != "description" && key != "parameters" && key != "script" && key != "enabled") {
                throw std::invalid_argument{ "unknown Lua tool field" };
            }
        }
        if (!value.isMember("enabled")) {
            value["enabled"] = true;
        }
        if (!value["enabled"].isBool() || !value["description"].isString() || value["description"].asString().size() > 4096 || !value["script"].isString() || value["script"].asString().empty() || value["script"].asString().size() > 256 * 1024) {
            throw std::invalid_argument{ "Lua tool requires bounded description, parameters, script, and enabled flag" };
        }
        schema_check(value["parameters"], nullptr);
        if (value["parameters"].get("type", "object") != "object") {
            throw std::invalid_argument{ "tool parameters must have object type" };
        }
        Budget budget{};
        auto state{ new_state(budget) };
        auto const script{ value["script"].asString() };
        if (luaL_loadbufferx(state.get(), script.data(), script.size(), name.c_str(), "t") != LUA_OK) {
            throw std::invalid_argument{ "Lua syntax error: " + std::string{ lua_tostring(state.get(), -1) } };
        }
        return value;
    }

    /**
     * @brief Validate arguments against the supported JSON Schema subset.
     * @param schema Parameter schema.
     * @param value Model-supplied argument value.
     * @throws std::invalid_argument For missing, unexpected, or mistyped arguments.
     */
    auto validate_arguments(Json::Value const& schema, Json::Value const& value) -> void {
        schema_check(schema, &value);
    }

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
        LuaHost host
    ) -> drogon::Task<Json::Value> {
        if (script.empty() || script.size() > 256 * 1024) {
            throw std::invalid_argument{ "Lua source must contain 1..262144 bytes" };
        }
        Budget budget{};
        auto state{ new_state(budget) };
        auto* main{ state.get() };
        lua_pushliteral(main, "run");
        auto* thread{ lua_newthread(main) };
        lua_sethook(thread, instruction_hook, LUA_MASKCOUNT, 1000);
        lua_checkstack(thread, 16);
        budget.resumed = std::chrono::steady_clock::now();
        if (luaL_loadbufferx(main, script.data(), script.size(), "custom-tool", "t") != LUA_OK || lua_pcall(main, 0, 0, 0) != LUA_OK) {
            throw std::runtime_error{ "Lua load failed: " + std::string{ lua_tostring(main, -1) ? lua_tostring(main, -1) : "unknown error" } };
        }
        budget.spent += std::chrono::steady_clock::now() - budget.resumed;
        lua_getglobal(thread, "run");
        if (!lua_isfunction(thread, -1)) {
            throw std::invalid_argument{ "Lua tool must define run(args, ctx)" };
        }
        protected_push(thread, args);
        protected_push(thread, context);
        int arguments{ 2 };
        int operations{};
        for (;;) {
            budget.resumed = std::chrono::steady_clock::now();
            int results{};
            auto const status{ lua_resume(thread, nullptr, arguments, &results) };
            budget.spent += std::chrono::steady_clock::now() - budget.resumed;
            if (status == LUA_OK) {
                auto value{ results > 0 ? from_lua(thread, -results) : Json::Value{} };
                if (common::json_text(value).size() > 1024 * 1024) {
                    throw std::runtime_error{ "Lua result exceeds 1 MiB" };
                }
                co_return value;
            }
            if (status != LUA_YIELD) {
                throw std::runtime_error{ "Lua tool failed: " + std::string{ lua_tostring(thread, -1) ? lua_tostring(thread, -1) : "unknown error" } };
            }
            if (++operations > 32 || results != 1 || !host) {
                throw std::runtime_error{ "Lua host operation budget exceeded or host is unavailable" };
            }
            auto operation{ from_lua(thread, -1) };
            if (!operation.isObject() || !operation["operation"].isString()) {
                throw std::invalid_argument{ "invalid Lua host operation" };
            }
            lua_settop(thread, 0);
            auto response{ co_await host(std::move(operation)) };
            protected_push(thread, response);
            arguments = 1;
        }
    }
} // namespace tools
