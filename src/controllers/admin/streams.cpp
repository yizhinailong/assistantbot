/**
 * @file streams.cpp
 * @brief Implement authenticated event and log WebSocket streams.
 */
#include "controllers/admin/streams.hpp"

#include <drogon/drogon.h>

#include "admin/auth.hpp"
#include "admin/live_events.hpp"

namespace controllers {

    /**
     * @brief Register a connection with its original session expiry.
     * @param request Authenticated upgrade request.
     * @param connection Open WebSocket.
     */
    auto StreamsController::handleNewConnection(
        drogon::HttpRequestPtr const& request,
        drogon::WebSocketConnectionPtr const& connection
    ) -> void {
        if (!drogon::app().getPlugin<admin::Auth>()->AttachStream(request, connection, request->path() == "/api/admin/logs/ws")) {
            connection->forceClose();
        }
    }

    /**
     * @brief Apply JSON subscriptions on an authenticated connection.
     * @param connection Open WebSocket.
     * @param message Incoming payload.
     * @param type WebSocket frame type.
     */
    auto StreamsController::handleNewMessage(
        drogon::WebSocketConnectionPtr const& connection,
        std::string&& message,
        drogon::WebSocketMessageType const& type
    ) -> void {
        if (type == drogon::WebSocketMessageType::Text) {
            admin::subscribe_stream(connection, message);
        } else if (type == drogon::WebSocketMessageType::Binary) {
            connection->forceClose();
        }
    }

    /**
     * @brief Remove a closed subscriber.
     * @param connection Closed WebSocket.
     */
    auto StreamsController::handleConnectionClosed(
        drogon::WebSocketConnectionPtr const& connection
    ) -> void {
        admin::close_stream(connection);
    }

} // namespace controllers
