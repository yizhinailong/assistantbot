/**
 * @file assets.cpp
 * @brief Use typed NapCat extensions and versioned character image storage.
 */
#include "controllers/admin/assets.hpp"

#include "admin/http_helpers.hpp"
#include "admin/http_traces.hpp"
#include "storage/database.hpp"
#include "storage/feature_records.hpp"
#include "workflow/character_image.hpp"

namespace controllers {
    /**
     * @brief List favourites or add a file already present on the NapCat host.
     * @param req Authenticated request.
     * @return Typed favourite list or action result.
     */
    auto AssetsController::Stickers(drogon::HttpRequestPtr req) const -> drogon::Task<drogon::HttpResponsePtr> {
        try {
            auto api{ admin::traced_onebot() };
            if (req->method() == drogon::Post) {
                auto const body{ req->getJsonObject() };
                if (!body || !(*body)["file"].isString() || (*body)["file"].asString().empty() || (*body)["file"].asString().size() > 4096) {
                    throw std::invalid_argument{ "file must be an existing path on the NapCat host" };
                }
                auto const result{ co_await api.AddCustomFace({ .file = (*body)["file"].asString() }) };
                if (!result) {
                    co_return admin::json_error(result.error().message, drogon::k502BadGateway);
                }
                co_return drogon::HttpResponse::newHttpJsonResponse(*result);
            }
            auto const count{ admin::query_integer(req, "count", 200, 1, 1000) };
            auto const result{ co_await api.FetchCustomFaceDetail({ .count = static_cast<std::int32_t>(count), .timeout = 5.0 }) };
            if (!result) {
                co_return admin::json_error(result.error().message, drogon::k502BadGateway);
            }
            Json::Value body{};
            body["stickers"] = Json::Value{ Json::arrayValue };
            for (auto const& face : *result) {
                Json::Value item{};
                item["emoji_id"] = face.emoji_id;
                item["res_id"] = face.res_id;
                item["md5"] = face.md5;
                item["url"] = face.url;
                item["description"] = face.description;
                body["stickers"].append(std::move(item));
            }
            co_return drogon::HttpResponse::newHttpJsonResponse(body);
        } catch (std::invalid_argument const& error) {
            co_return admin::json_error(error.what(), drogon::k400BadRequest);
        }
    }

    /**
     * @brief Update a favourite description or delete a resource.
     * @param req Authenticated request.
     * @param id NapCat resource identifier.
     * @return Action result or HTTP error.
     */
    auto AssetsController::Sticker(drogon::HttpRequestPtr req, std::string id) const -> drogon::Task<drogon::HttpResponsePtr> {
        try {
            if (id.empty() || id.size() > 1024) {
                throw std::invalid_argument{ "invalid resource identifier" };
            }
            auto api{ admin::traced_onebot() };
            if (req->method() == drogon::Delete) {
                auto const result{ co_await api.DeleteCustomFace({ .res_ids = { id } }) };
                if (!result) {
                    co_return admin::json_error(result.error().message, drogon::k502BadGateway);
                }
                co_return drogon::HttpResponse::newHttpJsonResponse(*result);
            }
            auto const body{ req->getJsonObject() };
            if (!body || !(*body)["emoji_id"].isString() || !(*body)["md5"].isString() || !(*body)["description"].isString() || (*body)["description"].asString().size() > 4096 || (*body)["md5"].asString().size() > 128) {
                throw std::invalid_argument{ "expected emoji_id, md5, and bounded description" };
            }
            static_cast<void>(admin::decimal((*body)["emoji_id"].asString()));
            auto const result{ co_await api.SetCustomFaceDesc({ .emoji_id = (*body)["emoji_id"].asString(), .res_id = std::move(id), .md5 = (*body)["md5"].asString(), .description = (*body)["description"].asString() }) };
            if (!result) {
                co_return admin::json_error(result.error().message, drogon::k502BadGateway);
            }
            co_return drogon::HttpResponse::newHttpJsonResponse(*result);
        } catch (std::invalid_argument const& error) {
            co_return admin::json_error(error.what(), drogon::k400BadRequest);
        }
    }

    /**
     * @brief Read, atomically replace, or delete the validated character image.
     * @param req Authenticated request.
     * @return Persisted reference document, or an empty reference.
     */
    auto AssetsController::Character(drogon::HttpRequestPtr req) const -> drogon::Task<drogon::HttpResponsePtr> {
        try {
            auto const db{ drogon::app().getPlugin<storage::Database>()->Client() };
            if (req->method() == drogon::Post) {
                auto const body{ req->getJsonObject() };
                if (!body || !(*body)["data_url"].isString() || !(*body)["revision"].isString()) {
                    throw std::invalid_argument{ "expected data_url and decimal revision" };
                }
                auto const data{ (*body)["data_url"].asString() };
                auto const bytes{ workflow::character_image_bytes(data) };
                Json::Value value{};
                value["data_url"] = data;
                value["bytes"] = Json::UInt64{ bytes.size() };
                co_return drogon::HttpResponse::newHttpJsonResponse(co_await storage::save_feature_record(db, "character", "main", std::move(value), admin::decimal((*body)["revision"].asString())));
            }
            if (req->method() == drogon::Delete) {
                std::optional<std::int64_t> expected{};
                if (req->getParameters().contains("revision")) {
                    expected = admin::query_integer(req, "revision", 0, 1);
                }
                if (!(co_await storage::delete_feature_record(db, "character", "main", expected)) && expected) {
                    co_return admin::json_error("character image changed; refresh before deleting", drogon::k409Conflict);
                }
            }
            auto const records{ co_await storage::feature_records(db, "character", "main") };
            if (!records.empty()) {
                co_return drogon::HttpResponse::newHttpJsonResponse(records[0]);
            }
            Json::Value empty{};
            empty["revision"] = "0";
            empty["value"] = Json::Value{};
            co_return drogon::HttpResponse::newHttpJsonResponse(empty);
        } catch (std::invalid_argument const& error) {
            co_return admin::json_error(error.what(), drogon::k400BadRequest);
        } catch (drogon::orm::DrogonDbException const& error) {
            LOG_ERROR << "Character image storage failed: " << error.base().what();
            co_return admin::json_error("could not save character image", drogon::k500InternalServerError);
        } catch (std::runtime_error const& error) {
            co_return admin::json_error(error.what(), drogon::k409Conflict);
        }
    }
} // namespace controllers
