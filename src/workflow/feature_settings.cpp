/**
 * @file feature_settings.cpp
 * @brief Validate optional capabilities while keeping credentials in environment variables.
 */
#include "workflow/feature_settings.hpp"

#include <array>
#include <cmath>
#include <regex>
#include <set>
#include <stdexcept>

#include "storage/settings.hpp"
#include "tools/catalog.hpp"
#include "workflow/access_control.hpp"

namespace workflow {

    namespace {

        constexpr std::array ROLES{ "router", "decision", "executor", "executor_fallback", "thinking", "vision", "image_generation", "memory", "embedding" }; ///< Supported model responsibilities.

        /**
         * @brief Reject unsupported object members before merging defaults.
         * @param value User-supplied section.
         * @param defaults Allowed keys and default values.
         * @param name Section name for validation errors.
         * @return Values overlaid on the defaults.
         */
        auto merge_object(
            Json::Value const& value,
            Json::Value defaults,
            std::string const& name
        ) -> Json::Value {
            if (!value.isObject()) {
                throw std::invalid_argument{ name + " must be an object" };
            }
            for (auto const& key : value.getMemberNames()) {
                if (!defaults.isMember(key)) {
                    throw std::invalid_argument{ name + ": unknown field " + key };
                }
                defaults[key] = value[key];
            }
            return defaults;
        }

        /**
         * @brief Validate a bounded integer setting.
         * @param value Complete section.
         * @param key Setting name.
         * @param minimum Smallest accepted value.
         * @param maximum Largest accepted value.
         */
        auto integer(Json::Value const& value, char const* key, int minimum, int maximum) -> void {
            if (!value[key].isInt() || value[key].asInt() < minimum || value[key].asInt() > maximum) {
                throw std::invalid_argument{ std::string{ key } + " must be an integer in [" + std::to_string(minimum) + ", " + std::to_string(maximum) + "]" };
            }
        }

        /**
         * @brief Validate a boolean setting.
         * @param value Complete section.
         * @param key Setting name.
         */
        auto boolean(Json::Value const& value, char const* key) -> void {
            if (!value[key].isBool()) {
                throw std::invalid_argument{ std::string{ key } + " must be a boolean" };
            }
        }

        /**
         * @brief Validate bounded text.
         * @param value Complete section.
         * @param key Setting name.
         * @param maximum Maximum UTF-8 byte count.
         */
        auto text(Json::Value const& value, char const* key, std::size_t maximum) -> void {
            if (!value[key].isString() || value[key].asString().size() > maximum || value[key].asString().find('\0') != std::string::npos) {
                throw std::invalid_argument{ std::string{ key } + " must be bounded text" };
            }
        }

        /**
         * @brief Validate a finite numeric setting.
         * @param value Complete section.
         * @param key Setting name.
         * @param minimum Minimum accepted value.
         * @param maximum Maximum accepted value.
         */
        auto number(
            Json::Value const& value,
            char const* key,
            double minimum,
            double maximum
        ) -> void {
            if (!value[key].isNumeric() || !std::isfinite(value[key].asDouble()) || value[key].asDouble() < minimum || value[key].asDouble() > maximum) {
                throw std::invalid_argument{ std::string{ key } + " must be a finite number in range" };
            }
        }

        /**
         * @brief Validate a credential environment-variable name without reading its value.
         * @param value Variable name; empty inherits or omits authentication.
         */
        auto environment_name(std::string const& value) -> void {
            static std::regex const NAME{ "[A-Za-z_][A-Za-z0-9_]{0,127}" };
            if (!value.empty() && !std::regex_match(value, NAME)) {
                throw std::invalid_argument{ "credential fields must name an environment variable" };
            }
        }

        /**
         * @brief Validate an HTTP endpoint without user information, query, or fragment.
         * @param value Endpoint URL.
         * @param optional Whether an empty endpoint may inherit defaults.
         */
        auto endpoint(std::string const& value, bool optional) -> void {
            static std::regex const URL{ R"(https?://[^\s/@?#]+(?:/[^\s?#]*)?)" };
            if ((value.empty() && optional) || std::regex_match(value, URL)) {
                return;
            }
            throw std::invalid_argument{ "endpoint must be an http(s) URL without credentials, query, or fragment" };
        }

        /**
         * @brief Normalize one configuration section and reject malformed values.
         * @param section Section name.
         * @param values User-supplied section.
         * @return Complete validated section.
         */
        auto validate(std::string const& section, Json::Value values) -> Json::Value {
            auto const defaults{ default_feature_settings() };
            if (!defaults.isMember(section) || section == "_revision") {
                throw std::invalid_argument{ "unknown feature section" };
            }
            if (section == "admins") {
                if (!values.isArray() || values.size() > 1000) {
                    throw std::invalid_argument{ "admins must be an array of at most 1000 QQ strings" };
                }
                std::set<std::string> normalized{};
                for (auto const& entry : values) {
                    if (!entry.isString()) {
                        throw std::invalid_argument{ "administrator QQ must be a string" };
                    }
                    normalized.insert(std::to_string(parse_qq(entry.asString())));
                }
                Json::Value result{ Json::arrayValue };
                for (auto const& entry : normalized) {
                    result.append(entry);
                }
                return result;
            }
            if (section == "groups") {
                if (!values.isObject() || values.size() > 10000) {
                    throw std::invalid_argument{ "groups must be an object of group configurations" };
                }
                Json::Value result{ Json::objectValue };
                for (auto const& id : values.getMemberNames()) {
                    Json::Value group{ Json::objectValue };
                    group["name"] = "";
                    group["enabled"] = false;
                    group["auto_reply"] = false;
                    group["include_card"] = true;
                    group["commands_enabled"] = true;
                    group["additional_prompt"] = "";
                    group = merge_object(values[id], std::move(group), "group");
                    text(group, "name", 256);
                    text(group, "additional_prompt", 16384);
                    for (auto const* key : { "enabled", "auto_reply", "include_card", "commands_enabled" }) {
                        boolean(group, key);
                    }
                    auto const normalized{ std::to_string(parse_qq(id)) };
                    if (result.isMember(normalized)) {
                        throw std::invalid_argument{ "duplicate normalized group QQ" };
                    }
                    result[normalized] = std::move(group);
                }
                return result;
            }
            values = merge_object(values, defaults[section], section);
            if (section == "models") {
                for (auto const* role : ROLES) {
                    values[role] = merge_object(values[role], defaults[section][role], role);
                    auto const& model{ values[role] };
                    boolean(model, "enabled");
                    text(model, "base_url", 2048);
                    text(model, "api_key_env", 128);
                    text(model, "model", 256);
                    text(model, "reasoning_effort", 32);
                    environment_name(model["api_key_env"].asString());
                    endpoint(model["base_url"].asString(), true);
                    integer(model, "timeout", 1, 3600);
                    integer(model, "max_retries", 0, 10);
                    integer(model, "max_tokens", 0, 1000000);
                    number(model, "temperature", 0, 2);
                }
            } else if (section == "execution") {
                integer(values, "max_tool_rounds", 1, 32);
                integer(values, "max_tool_result_chars", 256, 32000);
                integer(values, "mcp_page_size", 1, 100);
                integer(values, "mcp_max_tools_per_enable", 1, 32);
                integer(values, "mcp_max_tools_per_reply", 1, 64);
            } else if (section == "memory") {
                boolean(values, "enabled");
                boolean(values, "affinity_enabled");
                integer(values, "max_memories", 1, 20);
                integer(values, "maintenance_turns", 1, 100);
                number(values, "similarity_threshold", 0, 1);
            } else if (section == "tools") {
                boolean(values, "enabled");
                if (!values["builtin"].isObject()) {
                    throw std::invalid_argument{ "builtin must be an object" };
                }
                for (auto const& name : values["builtin"].getMemberNames()) {
                    if (!tools::is_builtin(name)) {
                        throw std::invalid_argument{ "unknown built-in tool: " + name };
                    }
                    Json::Value tool{};
                    tool["enabled"] = true;
                    tool["description"] = "";
                    tool = merge_object(values["builtin"][name], std::move(tool), "tool");
                    boolean(tool, "enabled");
                    text(tool, "description", 4096);
                    values["builtin"][name] = std::move(tool);
                }
            } else if (section == "mcp") {
                if (!values["servers"].isArray() || values["servers"].size() > 16) {
                    throw std::invalid_argument{ "servers must be an array of at most 16 services" };
                }
                std::set<std::string> names{};
                for (auto& server : values["servers"]) {
                    Json::Value config{};
                    config["name"] = "";
                    config["description"] = "";
                    config["url"] = "";
                    config["token_env"] = "";
                    config["enabled"] = true;
                    config["timeout"] = 30;
                    config["protocol_version"] = "2025-11-25";
                    server = merge_object(server, std::move(config), "MCP server");
                    text(server, "name", 32);
                    text(server, "description", 4096);
                    text(server, "url", 2048);
                    text(server, "token_env", 128);
                    boolean(server, "enabled");
                    integer(server, "timeout", 1, 300);
                    static std::regex const NAME{ "[A-Za-z][A-Za-z0-9_-]{0,31}" };
                    if (!std::regex_match(server["name"].asString(), NAME) || !names.insert(server["name"].asString()).second) {
                        throw std::invalid_argument{ "MCP service names must be unique identifiers" };
                    }
                    environment_name(server["token_env"].asString());
                    endpoint(server["url"].asString(), false);
                    if (server["protocol_version"] != "2025-11-25" && server["protocol_version"] != "2026-07-28") {
                        throw std::invalid_argument{ "unsupported MCP protocol version" };
                    }
                }
            }
            return values;
        }

    } // namespace

    /**
     * @brief Return defaults for groups, administrators, model roles, tools, MCP, and memory.
     * @return Complete configuration with optional capabilities disabled.
     */
    auto default_feature_settings() -> Json::Value {
        Json::Value root{ Json::objectValue };
        root["_revision"] = Json::UInt64{ 0 };
        root["groups"] = Json::Value{ Json::objectValue };
        root["admins"] = Json::Value{ Json::arrayValue };
        for (auto const* name : ROLES) {
            auto& role{ root["models"][name] };
            role["enabled"] = false;
            role["base_url"] = "";
            role["api_key_env"] = "";
            role["model"] = "";
            role["timeout"] = 90;
            role["max_retries"] = 0;
            role["max_tokens"] = 0;
            role["temperature"] = 0.7;
            role["reasoning_effort"] = "";
        }
        auto& execution{ root["execution"] };
        execution["max_tool_rounds"] = 6;
        execution["max_tool_result_chars"] = 4000;
        execution["mcp_page_size"] = 10;
        execution["mcp_max_tools_per_enable"] = 4;
        execution["mcp_max_tools_per_reply"] = 12;
        auto& memory{ root["memory"] };
        memory["enabled"] = false;
        memory["affinity_enabled"] = false;
        memory["max_memories"] = 3;
        memory["maintenance_turns"] = 10;
        memory["similarity_threshold"] = 0.45;
        root["tools"]["enabled"] = false;
        root["tools"]["builtin"] = Json::Value{ Json::objectValue };
        root["mcp"]["servers"] = Json::Value{ Json::arrayValue };
        return root;
    }

    /**
     * @brief Load saved optional-feature configuration during plugin initialization.
     * @param database Database running on its own connection thread.
     */
    FeatureSettings::FeatureSettings(drogon::orm::DbClientPtr database)
        : m_database{ std::move(database) } {
        auto config{ default_feature_settings() };
        if (auto saved{ storage::load_setting(*m_database, "features") }) {
            if (!saved->isObject()) {
                throw std::invalid_argument{ "saved feature settings must be an object" };
            }
            for (auto const& section : saved->getMemberNames()) {
                if (section != "_revision") {
                    config[section] = validate(section, (*saved)[section]);
                }
            }
            if (saved->isMember("_revision")) {
                if (!(*saved)["_revision"].isUInt64()) {
                    throw std::invalid_argument{ "invalid saved feature revision" };
                }
                config["_revision"] = (*saved)["_revision"];
            }
        }
        m_current = std::make_shared<Json::Value const>(std::move(config));
    }

    /**
     * @brief Copy the current immutable complete configuration pointer.
     * @return Snapshot valid across subsequent updates.
     */
    auto FeatureSettings::Current() const -> std::shared_ptr<Json::Value const> {
        std::lock_guard lock{ m_mutex };
        return m_current;
    }

    /**
     * @brief Describe a section and its optimistic concurrency revision.
     * @param section groups, admins, models, execution, memory, tools, or mcp.
     * @return Values, defaults, and decimal revision.
     * @throws std::invalid_argument If the section is unknown.
     */
    auto FeatureSettings::Describe(std::string const& section) const -> Json::Value {
        auto const current{ Current() };
        auto const defaults{ default_feature_settings() };
        if (!defaults.isMember(section) || section == "_revision") {
            throw std::invalid_argument{ "unknown feature section" };
        }
        Json::Value result{};
        result["values"] = (*current)[section];
        result["defaults"] = defaults[section];
        result["revision"] = std::to_string((*current)["_revision"].asUInt64());
        return result;
    }

    /**
     * @brief Validate, save, and publish one complete feature section.
     * @param section Feature section to replace.
     * @param values New section values; omitted optional fields use defaults.
     * @param revision Expected revision, or empty to accept the current revision.
     * @return Task completing after publication.
     * @throws std::invalid_argument If values are invalid.
     * @throws std::runtime_error If a write overlaps or the revision is stale.
     */
    auto FeatureSettings::Update(
        std::string section,
        Json::Value values,
        std::optional<std::uint64_t> revision
    ) -> drogon::Task<> {
        values = validate(section, std::move(values));
        Json::Value next{};
        {
            std::lock_guard lock{ m_mutex };
            if (m_writing || (revision && *revision != (*m_current)["_revision"].asUInt64())) {
                throw std::runtime_error{ "feature settings changed; refresh before saving" };
            }
            if ((*m_current)["_revision"].asUInt64() == UINT64_MAX) {
                throw std::runtime_error{ "feature revision exhausted" };
            }
            next = *m_current;
            next[section] = std::move(values);
            next["_revision"] = Json::UInt64{ next["_revision"].asUInt64() + 1 };
            m_writing = true;
        }
        try {
            co_await storage::save_setting(m_database, "features", next);
            auto snapshot{ std::make_shared<Json::Value const>(std::move(next)) };
            std::lock_guard lock{ m_mutex };
            m_current = std::move(snapshot);
            m_writing = false;
        } catch (...) {
            std::lock_guard lock{ m_mutex };
            m_writing = false;
            throw;
        }
    }

} // namespace workflow
