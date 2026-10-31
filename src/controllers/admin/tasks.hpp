/**
 * @file tasks.hpp
 * @brief Declare durable conversation task administration.
 */
#pragma once
#include <drogon/HttpController.h>
#include <drogon/utils/coroutine.h>

namespace controllers {
    /**
     * @brief List, schedule, and cancel UTC reminders.
     */
    class TasksController : public drogon::HttpController<TasksController> {
    public:
        METHOD_LIST_BEGIN
        ADD_METHOD_TO(TasksController::Tasks, "/api/admin/tasks", drogon::Get, drogon::Post);
        ADD_METHOD_TO(TasksController::Cancel, "/api/admin/tasks/{1}/cancel", drogon::Post);
        METHOD_LIST_END
        /**
         * @brief List tasks by conversation or create one durable reminder.
         * @param req Authenticated request.
         * @return Task records or created task.
         */
        auto Tasks(drogon::HttpRequestPtr req) const -> drogon::Task<drogon::HttpResponsePtr>;
        /**
         * @brief Cancel a task that has not started delivery.
         * @param req Authenticated request.
         * @param id Positive task identifier.
         * @return Cancellation acknowledgement or conflict.
         */
        auto Cancel(
            drogon::HttpRequestPtr req,
            std::string id
        ) const -> drogon::Task<drogon::HttpResponsePtr>;
    };
} // namespace controllers
