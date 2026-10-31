/**
 * @file model_roles.hpp
 * @brief Route model requests through immutable role settings and record every request's usage.
 */
#pragma once

#include <drogon/orm/DbClient.h>

#include <drogon_openai/openai_client.hpp>

namespace workflow {
    /**
     * @brief Resolve independent model roles without changing plugin configuration or exposing keys.
     */
    class ModelRoles {
    private:
        drogon_openai::OpenAI m_primary;     ///< Immutable default endpoint.
        drogon::orm::DbClientPtr m_database; ///< Usage database.
        std::string m_defaultModel;          ///< Default model name on failed primary requests.

    public:
        /**
         * @brief Bind role requests to the primary client and usage database.
         * @param primary Configured plugin client.
         * @param database Persistent database.
         * @param model Primary model name.
         */
        ModelRoles(drogon_openai::OpenAI primary, drogon::orm::DbClientPtr database, std::string model);

        /**
         * @brief Complete one chat request with a role and record success or failure.
         * @param snapshot Complete immutable feature settings.
         * @param role Requested role; disabled executor inherits the primary endpoint.
         * @param session Positive friend or negative group key.
         * @param params Owned conversation and request parameters.
         * @return Typed completion result; credential configuration errors throw.
         */
        auto Complete(Json::Value snapshot, std::string role, std::int64_t session, drogon_openai::ChatCompletionCreateParams params) const -> drogon::Task<drogon_openai::Result<drogon_openai::ChatCompletion>>;

        /**
         * @brief Embed text with the configured embedding role, recording token usage.
         * @param snapshot Complete feature settings.
         * @param session Conversation key.
         * @param text Input text.
         * @return Validated finite vector, or empty when the role is disabled or fails.
         */
        auto Embed(Json::Value snapshot, std::int64_t session, std::string text) const -> drogon::Task<std::vector<double>>;

        /**
         * @brief Generate an image or edit the configured character reference.
         * @param snapshot Complete feature settings.
         * @param session Conversation key.
         * @param prompt Image instructions.
         * @param reference Optional validated character data URL.
         * @return Image URL or OneBot base64 URI; throws on missing role or failed generation.
         */
        auto Image(Json::Value snapshot, std::int64_t session, std::string prompt, std::string reference = {}) const -> drogon::Task<std::string>;

        /**
         * @brief Identify the configured embedding vector space without including credentials.
         * @param snapshot Complete feature settings.
         * @return Endpoint and model identity for dimension-compatible retrieval.
         */
        [[nodiscard]] auto EmbeddingSpace(Json::Value const& snapshot) const -> std::string;

    private:
        /**
         * @brief Build an immutable client override with a per-role trace observer.
         * @param config Role settings.
         * @param role Role label.
         * @param session Conversation key.
         * @return Resolved client without forwarding a primary key to a custom endpoint.
         */
        auto client(Json::Value const& config, std::string const& role, std::int64_t session) const -> drogon_openai::OpenAI;

        /**
         * @brief Append request outcome and optional usage without failing the original operation.
         * @param session Conversation key.
         * @param model Reported or configured model.
         * @param succeeded Whether a usable response was received.
         * @param usage Provider usage JSON, or null for unavailable totals.
         * @return Task completing after the persistence attempt.
         */
        auto record(std::int64_t session, std::string model, bool succeeded, Json::Value usage) const -> drogon::Task<>;
    };
} // namespace workflow
