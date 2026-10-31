/**
 * @file http_traces.cpp
 * @brief Serve buffered request traces and JSON downloads.
 */
#include "controllers/admin/http_traces.hpp"

#include "admin/http_helpers.hpp"
#include "admin/http_traces.hpp"

namespace controllers {

    /**
     * @brief Read or clear redacted HTTP diagnostic records.
     * @param req Authenticated administrative request.
     * @return JSON records, an attachment, or a structured input error.
     */
    auto HttpTracesController::Traces(
        drogon::HttpRequestPtr req
    ) const -> drogon::Task<drogon::HttpResponsePtr> {
        try {
            if (req->method() == drogon::Delete) {
                admin::clear_http_traces();
            }
            auto response{ drogon::HttpResponse::newHttpJsonResponse(admin::read_http_traces(req->getParameter("source"))) };
            if (req->path().ends_with("/download")) {
                response->addHeader("Content-Disposition", "attachment; filename=assistantbot-http-traces.json");
            }
            co_return response;
        } catch (std::invalid_argument const& error) {
            co_return admin::json_error(error.what(), drogon::k400BadRequest);
        }
    }

} // namespace controllers
