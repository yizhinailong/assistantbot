/**
 * @file main.cpp
 * @brief Start the assistantbot service: the QQ private chat bot and its HTTP API.
 */
#include <algorithm>
#include <cstdlib>
#include <print>

#include <drogon/drogon.h>

#include "admin/auth.hpp"
#include "admin/runtime_state.hpp"
#include "controllers/onebot/assistant_bot.hpp"

namespace {

    /**
     * @brief Abort when config.json does not load the bot plugin.
     *
     * Drogon only logs an unknown plugin name and keeps running, which would leave an app
     * that serves HTTP but never answers a message. Call this from a beginning advice,
     * after the plugins have started.
     */
    auto check_bot_plugin() -> void {
        if (drogon::app().getPlugin<admin::Auth>() == nullptr || drogon::app().getPlugin<admin::Runtime>() == nullptr) {
            LOG_FATAL << "admin::Auth and admin::Runtime must be loaded; refusing to serve unprotected administrative routes";
            std::abort();
        }
        if (drogon::app().getPlugin<controllers::AssistantBot>() == nullptr) {
            LOG_FATAL << "controllers::AssistantBot is not loaded; add it to plugins in config.json";
            std::abort();
        }
    }

    /**
     * @brief Print the listener addresses and every registered route.
     *
     * Drogon fills its route table inside `app().run()`, so call this from a
     * beginning advice rather than before `run()`.
     */
    auto print_routes() -> void {
        for (auto const& listener : drogon::app().getListeners()) {
            std::println("Listening on http://{}", listener.toIpPort());
        }

        auto handlers{ drogon::app().getHandlersInfo() };
        std::ranges::sort(handlers);
        for (auto const& [path, method, description] : handlers) {
            std::println("{} {}  ({})", drogon::to_string_view(method), path, description);
        }
    }

} // namespace

/**
 * @brief Load config.json from the working directory and run the app until it quits.
 * @return 0 after drogon has stopped.
 */
auto main() -> int {
    drogon::app()
        .loadConfigFile("config.json")
        .registerBeginningAdvice(check_bot_plugin)
        .registerBeginningAdvice(print_routes)
        .run();
    return 0;
}
