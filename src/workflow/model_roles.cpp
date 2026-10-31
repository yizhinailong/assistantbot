/**
 * @file model_roles.cpp
 * @brief Resolve endpoint overrides, observe role traffic, and account for optional model calls.
 */
#include "workflow/model_roles.hpp"

#include <cmath>
#include <stdexcept>

#include <drogon/drogon.h>

#include "admin/auth.hpp"
#include "admin/http_traces.hpp"
#include "admin/live_events.hpp"
#include "common/time.hpp"
#include "storage/usage.hpp"
#include "workflow/character_image.hpp"

namespace workflow {
    /**
     * @brief Bind role requests to the primary client and usage database.
     * @param primary Configured plugin client.
     * @param database Persistent database.
     * @param model Primary model name.
     */
    ModelRoles::ModelRoles(drogon_openai::OpenAI primary, drogon::orm::DbClientPtr database, std::string model)
        : m_primary{ std::move(primary) }, m_database{ std::move(database) }, m_defaultModel{ std::move(model) } {}

    /**
     * @brief Build an immutable client override with a per-role trace observer.
     * @param config Role settings.
     * @param role Role label.
     * @param session Conversation key.
     * @return Resolved client without forwarding a primary key to a custom endpoint.
     */
    auto ModelRoles::client(
        Json::Value const& config,
        std::string const& role,
        std::int64_t session
    ) const -> drogon_openai::OpenAI {
        drogon_openai::OpenAIOptions options{};
        if (config.get("enabled", false).asBool()) {
            auto const endpoint{ config["base_url"].asString() };
            auto const environment{ config["api_key_env"].asString() };
            if (!endpoint.empty()) {
                options.base_url = endpoint;
                options.api_key = "";
                options.organization = "";
                options.project = "";
            }
            if (!environment.empty()) {
                auto key{ admin::read_env(environment) };
                if (key.empty()) {
                    throw std::runtime_error{ "credential environment variable is unset: " + environment };
                }
                admin::add_http_trace_secret(key);
                options.api_key = std::move(key);
            }
            if (!config["model"].asString().empty()) {
                options.model = config["model"].asString();
            }
            options.timeout = static_cast<double>(config["timeout"].asInt());
            options.max_retries = config["max_retries"].asInt();
        }
        return admin::traced_model(m_primary.WithOptions(std::move(options)), role, std::to_string(session));
    }

    /**
     * @brief Complete one chat request with a role and record success or failure.
     * @param snapshot Complete immutable feature settings.
     * @param role Requested role; disabled executor inherits the primary endpoint.
     * @param session Positive friend or negative group key.
     * @param params Owned conversation and request parameters.
     * @return Typed completion result; credential configuration errors throw.
     */
    auto ModelRoles::Complete(
        Json::Value snapshot,
        std::string role,
        std::int64_t session,
        drogon_openai::ChatCompletionCreateParams params
    ) const -> drogon::Task<drogon_openai::Result<drogon_openai::ChatCompletion>> {
        auto const& config{ snapshot["models"][role] };
        if (role != "executor" && !config["enabled"].asBool()) {
            throw std::invalid_argument{ "model role is disabled: " + role };
        }
        if (config.get("enabled", false).asBool()) {
            if (!config["model"].asString().empty()) {
                params.model = config["model"].asString();
            }
            params.timeout = static_cast<double>(config["timeout"].asInt());
            params.max_retries = config["max_retries"].asInt();
            params.temperature = config["temperature"].asDouble();
            if (config["max_tokens"].asInt() > 0) {
                params.max_tokens = config["max_tokens"].asInt();
            }
            if (!config["reasoning_effort"].asString().empty()) {
                params.reasoning_effort = config["reasoning_effort"].asString();
            }
        }
        auto const model{ params.model.value_or(m_defaultModel) };
        auto result{ co_await client(config, role, session).chat.completions.Create(std::move(params)) };
        co_await record(session, result ? result->model : model, result.has_value(), result && result->usage ? result->usage->raw : Json::Value{});
        co_return result;
    }

    /**
     * @brief Embed text with the configured embedding role, recording token usage.
     * @param snapshot Complete feature settings.
     * @param session Conversation key.
     * @param text Input text.
     * @return Validated finite vector, or empty when the role is disabled or fails.
     */
    auto ModelRoles::Embed(
        Json::Value snapshot,
        std::int64_t session,
        std::string text
    ) const -> drogon::Task<std::vector<double>> {
        auto const config{ snapshot["models"]["embedding"] };
        if (!config["enabled"].asBool()) {
            co_return std::vector<double>{};
        }
        auto const model{ config["model"].asString().empty() ? m_defaultModel : config["model"].asString() };
        auto const result{ co_await client(config, "embedding", session).embeddings.Create({ .input = std::move(text), .model = model, .encoding_format = "float" }) };
        co_await record(session, model, result.has_value(), result ? result->usage : Json::Value{});
        std::vector<double> vector{};
        if (!result || !result->data.isArray() || result->data.empty()) {
            co_return vector;
        }
        auto const& values{ result->data[0]["embedding"] };
        if (!values.isArray() || values.empty() || values.size() > 4096) {
            co_return vector;
        }
        for (auto const& entry : values) {
            if (!entry.isNumeric() || !std::isfinite(entry.asDouble())) {
                co_return std::vector<double>{};
            }
            vector.push_back(entry.asDouble());
        }
        co_return vector;
    }

    /**
     * @brief Generate an image or edit the configured character reference.
     * @param snapshot Complete feature settings.
     * @param session Conversation key.
     * @param prompt Image instructions.
     * @param reference Optional validated character data URL.
     * @return Image URL or OneBot base64 URI; throws on missing role or failed generation.
     */
    auto ModelRoles::Image(
        Json::Value snapshot,
        std::int64_t session,
        std::string prompt,
        std::string reference
    ) const -> drogon::Task<std::string> {
        auto const config{ snapshot["models"]["image_generation"] };
        if (!config["enabled"].asBool()) {
            throw std::invalid_argument{ "image_generation role is disabled" };
        }
        auto api{ client(config, "image_generation", session) };
        auto const model{ config["model"].asString().empty() ? m_defaultModel : config["model"].asString() };
        auto result{ reference.empty() ? co_await api.images.Generate({ .prompt = std::move(prompt), .model = model, .n = 1, .response_format = "b64_json", .max_retries = 0 }) : co_await api.images.Edit({ .image = drogon_openai::UploadFile{ .content = character_image_bytes(reference), .filename = "character", .content_type = reference.substr(5, reference.find(';') - 5) }, .prompt = "Preserve the appearance of the character in the reference image. " + prompt, .model = model, .n = 1, .response_format = "b64_json", .max_retries = 0 }) };
        co_await record(session, model, result.has_value(), result && result->usage ? *result->usage : Json::Value{});
        if (!result || !result->data || !result->data->isArray() || result->data->empty()) {
            throw std::runtime_error{ "image generation failed" };
        }
        auto const& image{ (*result->data)[0] };
        if (image["b64_json"].isString() && image["b64_json"].asString().size() <= 12 * 1024 * 1024) {
            co_return "base64://" + image["b64_json"].asString();
        }
        if (image["url"].isString() && (image["url"].asString().starts_with("https://") || image["url"].asString().starts_with("http://"))) {
            co_return image["url"].asString();
        }
        throw std::runtime_error{ "image generation returned no supported image" };
    }

    /**
     * @brief Identify the configured embedding vector space without including credentials.
     * @param snapshot Complete feature settings.
     * @return Endpoint and model identity for dimension-compatible retrieval.
     */
    auto ModelRoles::EmbeddingSpace(Json::Value const& snapshot) const -> std::string {
        auto const& config{ snapshot["models"]["embedding"] };
        return (config["base_url"].asString().empty() ? m_primary.Config().base_url : config["base_url"].asString()) + "|" + (config["model"].asString().empty() ? m_defaultModel : config["model"].asString());
    }

    /**
     * @brief Append request outcome and optional usage without failing the original operation.
     * @param session Conversation key.
     * @param model Reported or configured model.
     * @param succeeded Whether a usable response was received.
     * @param usage Provider usage JSON, or null for unavailable totals.
     * @return Task completing after the persistence attempt.
     */
    auto ModelRoles::record(
        std::int64_t session,
        std::string model,
        bool succeeded,
        Json::Value usage
    ) const -> drogon::Task<> {
        storage::UsageRecord record{ .timestamp = common::unix_seconds(), .user_id = session, .model = std::move(model), .succeeded = succeeded };
        auto read{ [&](char const* key) -> std::optional<std::int64_t> {
            return usage.isObject() && usage[key].isInt64() && usage[key].asInt64() >= 0 ? std::optional{ usage[key].asInt64() } : std::nullopt;
        } };
        record.prompt_tokens = read("prompt_tokens");
        record.completion_tokens = read("completion_tokens");
        record.total_tokens = read("total_tokens");
        record.cached_tokens = read("prompt_cache_hit_tokens");
        if (usage.isObject() && usage["prompt_tokens_details"].isObject() && usage["prompt_tokens_details"]["cached_tokens"].isInt64() && usage["prompt_tokens_details"]["cached_tokens"].asInt64() >= 0) {
            record.cached_tokens = usage["prompt_tokens_details"]["cached_tokens"].asInt64();
        }
        try {
            co_await storage::record_usage(m_database, std::move(record));
            Json::Value event{};
            event["type"] = "usage_updated";
            event["user_id"] = std::to_string(session);
            admin::publish_event(std::move(event));
        } catch (std::exception const& error) {
            LOG_ERROR << "Recording role usage failed: " << error.what();
        }
    }
} // namespace workflow
