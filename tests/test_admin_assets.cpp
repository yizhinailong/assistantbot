/**
 * @file test_admin_assets.cpp
 * @brief Exercise typed favourite mutations and validated, versioned character uploads.
 */
#include "admin_support.hpp"
#include "workflow/character_image.hpp"

using test_support::request_admin;
using test_support::success;

namespace {
    constexpr char const* IMAGE{ "data:image/png;base64,iVBORw0KGgoAAAANSUhEUgAAAAEAAAABCAQAAAC1HAwCAAAAC0lEQVR42mP8/x8AAwMCAO+jB9kAAAAASUVORK5CYII=" }; ///< Complete static PNG fixture.
}

DROGON_TEST(AssetRoutesRequireAuthenticationAndValidateInput) {
    CHECK(request_admin("/api/admin/stickers", drogon::Get, {}, "")->statusCode() == drogon::k401Unauthorized);
    CHECK(request_admin("/api/admin/character-image", drogon::Post, {}, "")->statusCode() == drogon::k401Unauthorized);
    CHECK(request_admin("/api/admin/stickers?count=0")->statusCode() == drogon::k400BadRequest);
    CHECK(request_admin("/api/admin/stickers", drogon::Post, test_support::field("file", ""))->statusCode() == drogon::k400BadRequest);
    CHECK(request_admin("/api/admin/stickers/resource", drogon::Put, Json::Value{ Json::objectValue })->statusCode() == drogon::k400BadRequest);
    CHECK_THROWS_AS(workflow::character_image_bytes("data:image/png;base64,aGVsbG8="), std::invalid_argument);
    CHECK_THROWS_AS(workflow::character_image_bytes("data:image/gif;base64,R0lGODlh"), std::invalid_argument);
}

DROGON_TEST(FavouritesListAndDescriptionUseNapCatExtensions) {
    auto const list{ success(request_admin("/api/admin/stickers")) };
    REQUIRE(list["stickers"].size() == 1);
    CHECK(list["stickers"][0]["emoji_id"] == "7");
    Json::Value body{};
    body["emoji_id"] = "7";
    body["md5"] = "abc";
    body["description"] = "new description";
    CHECK(request_admin("/api/admin/stickers/resource", drogon::Put, body)->statusCode() == drogon::k200OK);
    CHECK(request_admin("/api/admin/stickers/resource", drogon::Delete)->statusCode() == drogon::k200OK);
}

DROGON_TEST(CharacterImagePersistsAndRejectsStaleReplacementsAndDeletes) {
    auto const initial{ success(request_admin("/api/admin/character-image")) };
    CHECK(initial["revision"] == "0");
    Json::Value body{};
    body["data_url"] = IMAGE;
    body["revision"] = "0";
    auto const saved{ success(request_admin("/api/admin/character-image", drogon::Post, body)) };
    CHECK(saved["revision"] == "1");
    CHECK(saved["value"]["data_url"] == IMAGE);
    CHECK(request_admin("/api/admin/character-image", drogon::Post, body)->statusCode() == drogon::k409Conflict);
    CHECK(request_admin("/api/admin/character-image?revision=2", drogon::Delete)->statusCode() == drogon::k409Conflict);
    CHECK(success(request_admin("/api/admin/character-image"))["value"]["data_url"] == IMAGE);
    CHECK(success(request_admin("/api/admin/character-image?revision=1", drogon::Delete))["revision"] == "0");
}

auto main(int argc, char** argv) -> int {
    test_support::load_config();
    for (auto const* action : { "fetch_custom_face_detail", "set_custom_face_desc", "delete_custom_face" }) {
        drogon::app().registerHandler(std::string{ "/napcat/" } + action, [action](drogon::HttpRequestPtr) -> drogon::Task<drogon::HttpResponsePtr> {
            Json::Value body{};
            body["status"] = "ok";
            body["retcode"] = 0;
            body["data"] = Json::Value{};
            if (std::string_view{ action } == "fetch_custom_face_detail") {
                Json::Value sticker{};
                sticker["emoId"] = 7;
                sticker["resId"] = "resource";
                sticker["md5"] = "abc";
                sticker["url"] = "https://example.test/sticker.png";
                sticker["desc"] = "hello";
                body["data"].append(sticker);
            }
            co_return drogon::HttpResponse::newHttpJsonResponse(body);
        },
                                      { drogon::Post });
    }
    return test_support::run_tests_with_app(argc, argv);
}
