/**
 * @file http_traces.hpp
 * @brief Declare authenticated request diagnostic endpoints.
 */
#pragma once

#include <drogon/HttpController.h>
#include <drogon/utils/coroutine.h>

namespace controllers {

    /**
     * @brief Read, download, and clear the bounded process request ring.
     */
    class HttpTracesController : public drogon::HttpController<HttpTracesController> {
    public:
        METHOD_LIST_BEGIN
        ADD_METHOD_TO(HttpTracesController::Traces, "/api/admin/http-traces", drogon::Get, drogon::Delete);
        ADD_METHOD_TO(HttpTracesController::Traces, "/api/admin/http-traces/download", drogon::Get);
        METHOD_LIST_END

        /**
         * @brief Read or clear redacted HTTP diagnostic records.
         * @param req Authenticated administrative request.
         * @return JSON records, an attachment, or a structured input error.
         */
        auto Traces(drogon::HttpRequestPtr req) const -> drogon::Task<drogon::HttpResponsePtr>;
    };

} // namespace controllers
