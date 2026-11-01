/**
 * @file application.cpp
 * @brief Initialize application services and release them in dependency order.
 */
#include "application.hpp"

#include <format>
#include <stdexcept>

#include <drogon/drogon.h>

#include <drogon_onebot/onebot_client.hpp>
#include <drogon_openai/openai_client.hpp>

#include "servers/http_traces.hpp"

namespace app {

    /**
     * @brief Validate configuration and start services using the initialized client plugins.
     * @param config Application configuration with authentication, storage, and bot settings.
     * @throws std::invalid_argument If configuration or client dependencies are invalid.
     * @throws std::runtime_error If database initialization or saved settings fail.
     * @note A failed start releases services that were already initialized.
     */
    auto Application::initAndStart(Json::Value const& config) -> void {
        if (!config.isObject()) {
            throw std::invalid_argument{ "app::Application: config must be an object" };
        }
        for (auto const& key : config.getMemberNames()) {
            if (key != "admin_token_env" && key != "data_dir" && key != "bot") {
                throw std::invalid_argument{ std::format("app::Application: {} is not a setting", key) };
            }
        }
        auto* const onebot{ drogon::app().getPlugin<drogon_onebot::OneBotClient>() };
        auto* const openai{ drogon::app().getPlugin<drogon_openai::OpenAIClient>() };
        if (onebot == nullptr || openai == nullptr) {
            throw std::invalid_argument{ "app::Application: drogon_onebot::OneBotClient and drogon_openai::OpenAIClient must be loaded and listed under dependencies" };
        }
        try {
            m_auth.Start(config, {
                                     onebot->Config().access_token,
                                     onebot->Config().secret,
                                     openai->Config().api_key,
                                     openai->Config().webhook_secret,
                                 });
            m_database.Start(config);
            m_bot.Start(config["bot"], *onebot, *openai, m_database.Client());
            m_runtime.Start(m_bot, admin::traced_onebot(), openai->Config().model, m_database.Client());
        } catch (...) {
            shutdown();
            throw;
        }
    }

    /**
     * @brief Stop runtime sampling, bot work, database ownership, and authentication.
     */
    auto Application::shutdown() -> void {
        m_runtime.Stop();
        m_bot.Stop();
        m_database.Stop();
        m_auth.Stop();
    }

    /**
     * @brief Access the administrative authentication service.
     * @return Authentication service owned for the application plugin's lifetime.
     */
    auto Application::Auth() noexcept -> admin::Auth& {
        return m_auth;
    }

    /**
     * @brief Access the initialized SQLite service.
     * @return Database service owned for the application plugin's lifetime.
     */
    auto Application::Database() noexcept -> storage::Database& {
        return m_database;
    }

    /**
     * @brief Access the bot service and its shared workflows.
     * @return Bot service owned for the application plugin's lifetime.
     */
    auto Application::Bot() noexcept -> controllers::AssistantBot& {
        return m_bot;
    }

    /**
     * @brief Access runtime status and process sampling.
     * @return Runtime service owned for the application plugin's lifetime.
     */
    auto Application::Runtime() noexcept -> admin::Runtime& {
        return m_runtime;
    }

} // namespace app
