/**
 * @file main.cpp
 * @brief Start the assistantbot service: the QQ private chat bot and its HTTP API.
 */
#include <algorithm>
#include <cstdlib>
#include <print>

#include <drogon/drogon.h>

#include "application.hpp"

namespace {

    /**
     * @brief Abort when config.json does not load the application services.
     * @note Call after plugin initialization and before listeners accept requests.
     */
    auto check_application() -> void {
        if (drogon::app().getPlugin<app::Application>() == nullptr) {
            LOG_FATAL << "app::Application is not loaded; configure it with the OneBot and OpenAI dependencies before serving requests";
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
        .registerBeginningAdvice(check_application)
        .registerBeginningAdvice(print_routes)
        .run();
    return 0;
}
