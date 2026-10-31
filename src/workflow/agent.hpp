/**
 * @file agent.hpp
 * @brief Declare one bounded model/tool execution loop per conversation reply.
 */
#pragma once
#include <functional>

#include "workflow/feature_settings.hpp"
#include "workflow/memory.hpp"
#include "workflow/scheduled_tasks.hpp"

namespace workflow {
    /**
     * @brief Result retained in short-term context after a completed agent turn.
     */
    struct AgentResult {
        std::string text; ///< Visible text produced by the turn; empty for an intentional no-reply.
        bool delivered{}; ///< Whether text was already sent by a terminal or action tool.
        bool completed{}; ///< Whether the turn produced text, a delivery, or an intentional no-reply.
    };

    /**
     * @brief Typed delivery callback that appends every attempted outgoing message.
     */
    using AgentSend = std::function<drogon::Task<Json::Value>(drogon_onebot::Message, std::string)>;

    /**
     * @brief Resolve feature snapshots and run optional model roles, Lua, and selected MCP tools.
     */
    class AgentRuntime {
    private:
        std::shared_ptr<FeatureSettings> m_features; ///< Immutable configuration provider.
        std::shared_ptr<ModelRoles> m_models;        ///< Role requests and usage.
        std::shared_ptr<MemoryService> m_memory;     ///< Scoped persistent memory.
        std::shared_ptr<ScheduledTasks> m_tasks;     ///< Scoped durable reminders.
        drogon::orm::DbClientPtr m_database;         ///< Versioned custom tools and character reference.
        drogon_onebot::OneBot m_onebot;              ///< Typed OneBot actions.

    public:
        /**
         * @brief Bind shared services without retaining the chat workflow itself.
         * @param features Optional feature settings.
         * @param models Independent role clients.
         * @param memory Persistent memory service.
         * @param tasks Reminder service.
         * @param database Document database.
         * @param onebot Typed outgoing action client.
         */
        AgentRuntime(std::shared_ptr<FeatureSettings> features, std::shared_ptr<ModelRoles> models, std::shared_ptr<MemoryService> memory, std::shared_ptr<ScheduledTasks> tasks, drogon::orm::DbClientPtr database, drogon_onebot::OneBot onebot);
        /**
         * @brief Run one bounded turn with a single feature snapshot and close remote sessions.
         * @param peer Current recipient and actual sender.
         * @param params Reply settings and in-memory history.
         * @param prompt Current merged user prompt.
         * @param send Current-conversation delivery callback.
         * @return Visible reply or an intentional completed turn.
         */
        auto Run(
            Peer peer,
            drogon_openai::ChatCompletionCreateParams params,
            std::string prompt,
            AgentSend send
        ) -> drogon::Task<AgentResult>;
        /**
         * @brief Describe accepted message images using the optional vision role.
         * @param peer Sender and recipient.
         * @param urls Validated HTTP image URLs from this message.
         * @param question Accompanying user question.
         * @return Description, or empty when vision is disabled or fails.
         */
        auto DescribeImages(
            Peer peer,
            std::vector<std::string> urls,
            std::string question
        ) -> drogon::Task<std::string>;
    };
} // namespace workflow
