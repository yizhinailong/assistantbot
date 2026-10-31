/**
 * @file streams.hpp
 * @brief Declare authenticated event and log WebSocket streams.
 */
#pragma once

#include <drogon/WebSocketController.h>

namespace controllers {

    /**
     * @brief Serve private conversation events and filtered process logs.
     */
    class StreamsController : public drogon::WebSocketController<StreamsController> {
    public:
        WS_PATH_LIST_BEGIN
        WS_PATH_ADD("/api/admin/ws");
        WS_PATH_ADD("/api/admin/logs/ws");
        WS_PATH_LIST_END

        /**
         * @brief Apply JSON subscriptions on an authenticated connection.
         * @param connection Open WebSocket.
         * @param message Incoming payload.
         * @param type WebSocket frame type.
         */
        auto handleNewMessage(
            drogon::WebSocketConnectionPtr const& connection,
            std::string&& message,
            drogon::WebSocketMessageType const& type
        ) -> void override;

        /**
         * @brief Register a connection with its original session expiry.
         * @param request Authenticated upgrade request.
         * @param connection Open WebSocket.
         */
        auto handleNewConnection(
            drogon::HttpRequestPtr const& request,
            drogon::WebSocketConnectionPtr const& connection
        ) -> void override;

        /**
         * @brief Remove a closed subscriber.
         * @param connection Closed WebSocket.
         */
        auto handleConnectionClosed(
            drogon::WebSocketConnectionPtr const& connection
        ) -> void override;
    };

} // namespace controllers
