/**
 * @file test_lua.cpp
 * @brief Exercise Lua host yielding, isolation, schemas, and resource exhaustion without side effects.
 */
#include <drogon/drogon_test.h>

#include "common/json.hpp"
#include "tools/lua.hpp"

DROGON_TEST(LuaJsonAndYieldingHosts) {
    Json::Value args{};
    args["name"] = "friend";
    int calls{};
    auto result{ drogon::sync_wait(tools::run_lua(R"lua(
function run(args,ctx)
  local r = http.post('https://example.invalid', json.encode({name=args.name}))
  local value = json.decode(r.body)
  bot.send_message(value.name)
  return {name=value.name, private=ctx.is_private, escaped=(os == nil and io == nil and package == nil and debug == nil and load == nil and coroutine == nil)}
end
)lua",
                                                  args,
                                                  common::parse_json(R"({"is_private":true})"),
                                                  [&calls](Json::Value operation) -> drogon::Task<Json::Value> {
                                                      ++calls;
                                                      Json::Value response{};
                                                      response["body"] = operation.get("body", "");
                                                      response["ok"] = true;
                                                      co_return response;
                                                  })) };
    CHECK(calls == 2);
    CHECK(result["name"] == "friend");
    CHECK(result["private"].asBool());
    CHECK(result["escaped"].asBool());
    CHECK(drogon::sync_wait(tools::run_lua("function run() return _G.previous == nil end", {}, {})).asBool());
}

DROGON_TEST(LuaBudgetsRejectUnboundedWorkAndAliasedJson) {
    for (auto const& script : {
             "function run() while true do end end",
             "function run() return string.rep('a', 32*1024*1024) end",
             "function run() local x={s=string.rep('x',1000)}; for i=1,30 do x={x,x} end; return x end",
             "function run() local x={}; x.x=x; return x end",
             "function run() return 0/0 end",
             "function run() while true do bot.send_message('x') end end" }) {
        bool rejected{};
        try {
            static_cast<void>(drogon::sync_wait(tools::run_lua(script, {}, {}, [](Json::Value) -> drogon::Task<Json::Value> { co_return Json::Value{}; })));
        } catch (std::exception const&) {
            rejected = true;
        }
        CHECK(rejected);
    }
    CHECK(drogon::sync_wait(tools::run_lua("function run() return 'still works' end", {}, {})) == "still works");
}

DROGON_TEST(LuaMetadataAndSchemasRejectMalformedInput) {
    Json::Value doc{ common::parse_json(R"({"description":"Test","script":"function run(args) return args.count end","parameters":{"type":"object","properties":{"count":{"type":"integer","minimum":1,"maximum":3}},"required":["count"],"additionalProperties":false}})") };
    CHECK(tools::validate_lua_tool("counter", doc)["enabled"].asBool());
    CHECK_THROWS_AS(static_cast<void>(tools::validate_lua_tool("reply", doc)), std::invalid_argument);
    auto const schema{ doc["parameters"] };
    CHECK_NOTHROW(tools::validate_arguments(schema, common::parse_json(R"({"count":2})")));
    CHECK_THROWS_AS(tools::validate_arguments(schema, common::parse_json(R"({"count":4})")), std::invalid_argument);
    CHECK_THROWS_AS(tools::validate_arguments(schema, common::parse_json(R"({"count":2,"extra":true})")), std::invalid_argument);
    CHECK_THROWS_AS(tools::validate_arguments(schema, Json::Value{ Json::objectValue }), std::invalid_argument);
    doc["parameters"]["type"] = true;
    CHECK_THROWS_AS(static_cast<void>(tools::validate_lua_tool("counter", doc)), std::invalid_argument);
}

auto main(int argc, char** argv) -> int {
    return drogon::test::run(argc, argv);
}
