/**
 * @file application.hpp
 * @brief Own the application services behind one Drogon plugin.
 */
#pragma once

#include <drogon/plugins/Plugin.h>
#include <json/value.h>

#include "controllers/onebot/assistant_bot.hpp"
#include "servers/auth.hpp"
#include "servers/runtime_state.hpp"
#include "storage/database.hpp"

namespace app {

    /**
     * @brief Initialize and expose the bot's authentication, database, bot, and runtime services.
     *
     * Register after the OneBot and OpenAI plugins and list both as dependencies.
     * Configuration contains `admin_token_env`, optional `data_dir`, and optional `bot`
     * reply defaults. Services start before listeners and stop in reverse order.
     */
    class Application : public drogon::Plugin<Application> {
    private:
        admin::Auth m_auth;              ///< Administrative authentication and event streams.
        storage::Database m_database;    ///< SQLite connection shared by the application services.
        controllers::AssistantBot m_bot; ///< Message registration and chat workflow ownership.
        admin::Runtime m_runtime;        ///< Runtime queries and periodic process samples.

    public:
        /**
         * @brief Validate configuration and start services using the initialized client plugins.
         * @param config Application configuration with authentication, storage, and bot settings.
         * @throws std::invalid_argument If configuration or client dependencies are invalid.
         * @throws std::runtime_error If database initialization or saved settings fail.
         * @note A failed start releases services that were already initialized.
         */
        auto initAndStart(Json::Value const& config) -> void override;

        /**
         * @brief Stop runtime sampling, bot work, database ownership, and authentication.
         */
        auto shutdown() -> void override;

        /**
         * @brief Access the administrative authentication service.
         * @return Authentication service owned for the application plugin's lifetime.
         */
        [[nodiscard]] auto Auth() noexcept -> admin::Auth&;

        /**
         * @brief Access the initialized SQLite service.
         * @return Database service owned for the application plugin's lifetime.
         */
        [[nodiscard]] auto Database() noexcept -> storage::Database&;

        /**
         * @brief Access the bot service and its shared workflows.
         * @return Bot service owned for the application plugin's lifetime.
         */
        [[nodiscard]] auto Bot() noexcept -> controllers::AssistantBot&;

        /**
         * @brief Access runtime status and process sampling.
         * @return Runtime service owned for the application plugin's lifetime.
         */
        [[nodiscard]] auto Runtime() noexcept -> admin::Runtime&;
    };

} // namespace app
