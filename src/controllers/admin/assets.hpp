/**
 * @file assets.hpp
 * @brief Declare favourite sticker and character image administration.
 */
#pragma once
#include <drogon/HttpController.h>
#include <drogon/utils/coroutine.h>

namespace controllers {
    /**
     * @brief Manage NapCat favourites and the persisted character reference.
     */
    class AssetsController : public drogon::HttpController<AssetsController> {
    public:
        METHOD_LIST_BEGIN
        ADD_METHOD_TO(AssetsController::Stickers, "/api/admin/stickers", drogon::Get, drogon::Post);
        ADD_METHOD_TO(AssetsController::Sticker, "/api/admin/stickers/{1}", drogon::Put, drogon::Delete);
        ADD_METHOD_TO(AssetsController::Character, "/api/admin/character-image", drogon::Get, drogon::Post, drogon::Delete);
        METHOD_LIST_END

        /**
         * @brief List favourites or add a file already present on the NapCat host.
         * @param req Authenticated request.
         * @return Typed favourite list or action result.
         */
        auto Stickers(drogon::HttpRequestPtr req) const -> drogon::Task<drogon::HttpResponsePtr>;
        /**
         * @brief Update a favourite description or delete a resource.
         * @param req Authenticated request.
         * @param id NapCat resource identifier.
         * @return Action result or HTTP error.
         */
        auto Sticker(drogon::HttpRequestPtr req, std::string id) const -> drogon::Task<drogon::HttpResponsePtr>;
        /**
         * @brief Read, atomically replace, or delete the validated character image.
         * @param req Authenticated request.
         * @return Persisted reference document, or an empty reference.
         */
        auto Character(drogon::HttpRequestPtr req) const -> drogon::Task<drogon::HttpResponsePtr>;
    };
} // namespace controllers
