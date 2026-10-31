/**
 * @file support.hpp
 * @brief Share the helpers of the test programs: the test configuration, the app thread, and signed events.
 */
#pragma once

#include <chrono>
#include <cstddef>
#include <cstdlib>
#include <filesystem>
#include <format>
#include <fstream>
#include <functional>
#include <future>
#include <iterator>
#include <stdexcept>
#include <string>
#include <string_view>
#include <thread>

#include <drogon/drogon.h>
#include <drogon/drogon_test.h>
#include <json/json.h>
#include <trantor/utils/Utilities.h>

namespace test_support {

    constexpr char const* ADMIN_TOKEN_ENV{ "ASSISTANTBOT_TEST_ADMIN_TOKEN" }; ///< Test token variable.
    constexpr char const* ADMIN_TOKEN{ "assistantbot-test-admin-token" };     ///< Test token.

    constexpr auto LISTEN_DELAY{ std::chrono::milliseconds{ 200 } };          ///< Wait for the listeners after the beginning advices.
    constexpr char const* CONFIG_PATH{ "tests/config.json" };                 ///< Test configuration, relative to the repo root that CTest runs in.
    constexpr char const* SERVER_URL{ "http://127.0.0.1:18082" };             ///< Listener in `CONFIG_PATH`.
    constexpr char const* EVENT_PATH{ "/onebot/event" };                      ///< `event_path` in `CONFIG_PATH`.
    constexpr char const* SECRET_ENV{ "ASSISTANTBOT_TEST_SECRET" };           ///< `secret_env` in `CONFIG_PATH`.
    constexpr char const* SECRET{ "assistantbot-test-secret" };               ///< Value that the tests give `SECRET_ENV`.
    constexpr char const* DATA_DIR{ "/tmp/assistantbot-test/data" };          ///< `data_dir` of `storage::Database` in `CONFIG_PATH`.

    /**
     * @brief Set an environment variable of this process.
     * @param name Variable name.
     * @param value New value.
     */
    inline auto set_env(char const* name, char const* value) -> void {
#ifdef _WIN32
        _putenv_s(name, value);
#else
        setenv(name, value, 1);
#endif
    }

    /**
     * @brief Load `CONFIG_PATH` into the app, after setting the variable that its OneBot plugin
     * reads.
     *
     * Also removes `DATA_DIR`, so that every app-backed test program starts with a new database.
     */
    inline auto load_config() -> void {
        set_env(SECRET_ENV, SECRET);
        set_env(ADMIN_TOKEN_ENV, ADMIN_TOKEN);
        std::filesystem::remove_all(DATA_DIR);
        drogon::app().loadConfigFile(CONFIG_PATH);
    }

    /**
     * @brief Log in through the public administrative authentication endpoint.
     * @return Opaque session cookie value.
     * @throws std::runtime_error If login fails.
     */
    inline auto login_admin() -> std::string {
        Json::Value body{};
        body["token"] = ADMIN_TOKEN;
        auto request{ drogon::HttpRequest::newHttpJsonRequest(body) };
        request->setMethod(drogon::Post);
        request->setPath("/api/admin/auth/login");
        auto const client{ drogon::HttpClient::newHttpClient(SERVER_URL) };
        auto const [result, response]{ client->sendRequest(request, 5.0) };
        if (result != drogon::ReqResult::Ok || response->statusCode() != drogon::k200OK) {
            throw std::runtime_error{ "administrative test login failed" };
        }
        auto const cookies{ response->cookies() };
        auto const found{ cookies.find("assistantbot_admin_session") };
        if (found == cookies.end()) {
            throw std::runtime_error{ "administrative login response omitted its session cookie" };
        }
        return found->second.value();
    }

    /**
     * @brief Give a test an empty directory of its own, for a database it opens itself.
     * @param name Name of the directory, unique among the tests.
     * @return `assistantbot-test/<name>` under the system's temporary directory, emptied if it
     * existed.
     */
    inline auto fresh_directory(std::string_view name) -> std::filesystem::path {
        auto const path{ std::filesystem::temp_directory_path() / "assistantbot-test" / name };
        std::filesystem::remove_all(path);
        std::filesystem::create_directories(path);
        return path;
    }

    /**
     * @brief Parse a JSON file, relative to the repo root that CTest runs in.
     * @param path File to read.
     * @return The parsed document.
     * @throws Json::RuntimeError If the file is missing or not valid JSON.
     */
    inline auto read_json(char const* path) -> Json::Value {
        std::ifstream file{ path };
        Json::Value root{};
        file >> root;
        return root;
    }

    /**
     * @brief Tell whether the arguments make `drogon::test::run()` print and exit.
     * @param argc Argument count.
     * @param argv Arguments.
     * @return True when one of them is `-l`, `-h`, or `--help`.
     */
    inline auto prints_and_exits(int argc, char** argv) -> bool {
        for (int i{ 1 }; i < argc; ++i) {
            std::string_view const arg{ argv[i] };
            if (arg == "-l" || arg == "-h" || arg == "--help") {
                return true;
            }
        }
        return false;
    }

    /**
     * @brief Run the app on a background thread, run every `DROGON_TEST`, then stop the app.
     *
     * Follows drogon's own app-backed tests (`lib/tests/main_CookieSameSite.cc`). Beginning
     * advices run before `startListening()`, and each IO loop starts listening
     * asynchronously, so the tests start `LISTEN_DELAY` after the advices. The test thread
     * runs no event loop, so tests may block on `drogon::sync_wait()` or
     * `HttpClient::sendRequest()`.
     *
     * `drogon::test::run()` answers `-l` and `-h` with `exit()` (`drogon_test.cc`), which
     * would end the process while the app thread runs, so those run without the app.
     * @param argc Argument count, forwarded to `drogon::test::run()`.
     * @param argv Arguments, forwarded to `drogon::test::run()`: `-s` prints passing checks,
     * `-r <name>` runs one test, and `-l` lists them.
     * @param before_quit Optional barrier for background work that outlives fake HTTP replies.
     * @return 0 if every check passed, 1 otherwise.
     */
    inline auto run_tests_with_app(
        int argc,
        char** argv,
        std::function<void()> before_quit = {}
    ) -> int {
        if (prints_and_exits(argc, argv)) {
            return drogon::test::run(argc, argv);
        }

        std::promise<void> started{};
        drogon::app().registerBeginningAdvice([&started] { started.set_value(); });
        std::thread server{ [] { drogon::app().run(); } };
        started.get_future().wait();
        std::this_thread::sleep_for(LISTEN_DELAY);

        int status{ drogon::test::run(argc, argv) };
        try {
            if (before_quit) {
                before_quit();
            }
        } catch (std::exception const& error) {
            LOG_ERROR << "Waiting for test background work failed: " << error.what();
            status = 1;
        }
        drogon::app().getLoop()->queueInLoop([] { drogon::app().quit(); });
        server.join();
        return status;
    }

    /**
     * @brief Compute HMAC-SHA1 (RFC 2104) as hex, the way NapCat signs the events it posts.
     * @param key Secret key.
     * @param message Signed bytes.
     * @return 40 lowercase hex digits.
     */
    inline auto hmac_sha1_hex(std::string_view key, std::string_view message) -> std::string {
        constexpr std::size_t BLOCK_SIZE{ 64 };
        std::string block{ key };
        if (block.size() > BLOCK_SIZE) {
            auto const digest{ trantor::utils::sha1(block) };
            block.assign(reinterpret_cast<char const*>(digest.bytes), sizeof(digest.bytes));
        }
        block.resize(BLOCK_SIZE, '\0');

        std::string inner{};
        std::string outer{};
        for (auto const byte : block) {
            inner.push_back(static_cast<char>(byte ^ 0x36));
            outer.push_back(static_cast<char>(byte ^ 0x5c));
        }
        inner.append(message);
        auto const inner_digest{ trantor::utils::sha1(inner) };
        outer.append(reinterpret_cast<char const*>(inner_digest.bytes), sizeof(inner_digest.bytes));
        auto const digest{ trantor::utils::sha1(outer) };

        std::string hex{};
        for (auto const byte : digest.bytes) {
            std::format_to(std::back_inserter(hex), "{:02x}", byte);
        }
        return hex;
    }

    /**
     * @brief Post an event to the bot's event route, signed with `SECRET` as NapCat would.
     *
     * Blocks on `HttpClient::sendRequest`, which is safe on the test thread because the
     * client runs on the app's main loop.
     * @param event Event object.
     * @return The route's response.
     * @throws std::runtime_error If no response arrives.
     */
    inline auto post_event(Json::Value const& event) -> drogon::HttpResponsePtr {
        Json::StreamWriterBuilder builder{};
        builder["indentation"] = "";
        auto const body{ Json::writeString(builder, event) };

        auto request{ drogon::HttpRequest::newHttpRequest() };
        request->setMethod(drogon::Post);
        request->setPath(EVENT_PATH);
        request->setContentTypeCode(drogon::CT_APPLICATION_JSON);
        request->setBody(body);
        request->addHeader("X-Signature", "sha1=" + hmac_sha1_hex(SECRET, body));
        auto const client{ drogon::HttpClient::newHttpClient(SERVER_URL) };
        auto const [result, response]{ client->sendRequest(request, 5.0) };
        if (result != drogon::ReqResult::Ok) {
            throw std::runtime_error{ std::format("POST {}: {}", EVENT_PATH, drogon::to_string_view(result)) };
        }
        return response;
    }

} // namespace test_support
