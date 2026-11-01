/**
 * @file test_application.cpp
 * @brief Verify service composition, configuration boundaries, and shutdown ownership.
 */
#include <memory>
#include <stdexcept>

#include "application.hpp"
#include "storage/schema.hpp"
#include "support.hpp"
#include "workflow/bot_settings.hpp"

DROGON_TEST(ApplicationOwnsAllInternalServices) {
    auto* const application{ drogon::app().getPlugin<app::Application>() };
    REQUIRE(application != nullptr);
    for (auto const* name : { "admin::Auth", "storage::Database", "controllers::AssistantBot", "admin::Runtime" }) {
        CHECK(drogon::app().getPlugin(name) == nullptr);
    }
    auto const database{ application->Database().Client() };
    REQUIRE(database != nullptr);
    CHECK(storage::schema_version(*database) == storage::latest_schema_version());
    REQUIRE(application->Bot().Settings() != nullptr);
    CHECK(application->Bot().Settings()->Current()->history_turns == 2);
    CHECK(!application->Runtime().ProcessMetrics()["samples"].empty());

    auto const session{ application->Auth().Login(test_support::ADMIN_TOKEN) };
    REQUIRE(!session.empty());
    CHECK(application->Auth().Authenticated(session));
    application->Auth().Logout(session);
    CHECK(!application->Auth().Authenticated(session));

    application->Runtime().SetPaused(true);
    CHECK(application->Bot().Paused());
    application->Bot().SetPaused(false);
    CHECK(!application->Runtime().Paused());
}

DROGON_TEST(ApplicationRejectsInvalidRootSettingsBeforeStartingServices) {
    app::Application application{};
    CHECK_THROWS_AS(application.initAndStart(Json::Value{}), std::invalid_argument);
    CHECK_THROWS_AS(application.initAndStart(Json::Value{ Json::arrayValue }), std::invalid_argument);
    Json::Value misplaced{ Json::objectValue };
    misplaced["history_turns"] = 2;
    CHECK_THROWS_AS(application.initAndStart(misplaced), std::invalid_argument);
    CHECK(application.Database().Client() == nullptr);
    CHECK(application.Bot().Settings() == nullptr);
    CHECK(application.Runtime().ProcessMetrics()["samples"].empty());
}

auto main(int argc, char** argv) -> int {
    test_support::load_config();
    std::weak_ptr<drogon::orm::DbClient> database{};
    std::weak_ptr<workflow::PrivateChat> chat{};
    std::weak_ptr<workflow::BotSettings> settings{};
    std::weak_ptr<workflow::ScheduledTasks> tasks{};
    auto const result{ test_support::run_tests_with_app(argc, argv, [&] {
        auto& application{ *drogon::app().getPlugin<app::Application>() };
        database = application.Database().Client();
        chat = application.Bot().Chat();
        settings = application.Bot().Settings();
        tasks = application.Bot().Tasks();
    }) };
    if (!database.expired() || !chat.expired() || !settings.expired() || !tasks.expired()) {
        LOG_ERROR << "Application shutdown retained database or bot services";
        return 1;
    }
    return result;
}
