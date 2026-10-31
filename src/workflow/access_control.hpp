/**
 * @file access_control.hpp
 * @brief Declare persistent friend access policies and immutable snapshots.
 */
#pragma once

#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <set>
#include <stdexcept>
#include <string_view>

#include <drogon/orm/DbClient.h>
#include <drogon/utils/coroutine.h>

namespace workflow {

    constexpr char const* ACCESS_SETTINGS_NAME{ "access" }; ///< Settings row holding the whole access policy.

    /**
     * @brief Choose whether every friend or only listed friends can receive replies.
     */
    enum class AccessMode { AllFriends,
                            Allowlist };

    /**
     * @brief Identify one of the two independently editable lists.
     */
    enum class AccessList { Allowlist,
                            Blacklist };

    /**
     * @brief Signal that another policy write is still in flight.
     */
    class AccessConflict : public std::runtime_error {
    public:
        using std::runtime_error::runtime_error;
    };

    /**
     * @brief Parse a positive decimal QQ number without losing integer precision.
     * @param text Decimal digits, with no sign or whitespace.
     * @return QQ number in the signed 64-bit range.
     * @throws std::invalid_argument If the number is invalid or out of range.
     */
    auto parse_qq(std::string_view text) -> std::int64_t;

    /**
     * @brief Parse an administrative access mode name.
     * @param name Either `all_friends` or `allowlist`.
     * @return Corresponding mode.
     * @throws std::invalid_argument If the name is unknown.
     */
    auto parse_access_mode(std::string_view name) -> AccessMode;

    /**
     * @brief Get the JSON name of an access mode.
     * @param mode Mode to serialize.
     * @return `all_friends` or `allowlist`.
     */
    auto access_mode_name(AccessMode mode) -> char const*;

    /**
     * @brief Hold one complete policy; a blacklist entry always takes priority.
     */
    struct AccessPolicy {
        AccessMode mode{ AccessMode::AllFriends }; ///< Default preserves replies to all friends.
        std::set<std::int64_t> allowlist;          ///< Friends allowed in allowlist mode.
        std::set<std::int64_t> blacklist;          ///< Senders blocked in both modes.

        /**
         * @brief Decide whether a friend's new message may reach commands or the queue.
         * @param qq Sender's QQ number.
         * @return True when the policy permits a reply.
         */
        [[nodiscard]] auto Allows(std::int64_t qq) const -> bool;
    };

    /**
     * @brief Save whole policies before publishing immutable snapshots to message handlers.
     *
     * One mutex protects short reads and write claims, never a database await. Concurrent
     * writes are rejected so memory and storage cannot commit in different orders. Share
     * this object through a shared pointer while its member coroutines run.
     */
    class AccessControl {
    private:
        drogon::orm::DbClientPtr m_db;                 ///< Database holding the policy.
        mutable std::mutex m_mutex;                    ///< Guards the members below.
        std::shared_ptr<AccessPolicy const> m_current; ///< Last successfully saved policy.
        bool m_saving{ false };                        ///< Whether a writer has claimed the policy.

        /**
         * @brief Copy, mutate, save, and publish one policy under an exclusive write claim.
         * @param change Mutation applied to a private copy of the current policy.
         * @return Newly saved snapshot.
         * @throws AccessConflict If another write is in flight.
         * @throws drogon::orm::DrogonDbException If saving fails; the snapshot stays unchanged.
         */
        auto update(
            std::function<void(AccessPolicy&)> change
        ) -> drogon::Task<std::shared_ptr<AccessPolicy const>>;

    public:
        /**
         * @brief Read the saved policy, or default to all friends with empty lists.
         * @param db Database with the settings table.
         * @throws std::invalid_argument If the saved policy is invalid.
         * @throws std::runtime_error If the saved JSON is malformed.
         * @throws drogon::orm::DrogonDbException If reading fails.
         * @note Blocks; call only before event loops start, during plugin initialization.
         */
        explicit AccessControl(drogon::orm::DbClientPtr db);

        /**
         * @brief Get the policy in effect without querying storage.
         * @return Immutable snapshot unaffected by later changes.
         */
        [[nodiscard]] auto Current() const -> std::shared_ptr<AccessPolicy const>;

        /**
         * @brief Save the mode while preserving both lists.
         * @param mode New access mode.
         * @return Newly saved snapshot.
         * @throws AccessConflict If another write is in flight.
         * @throws drogon::orm::DrogonDbException If saving fails.
         */
        auto SetMode(AccessMode mode) -> drogon::Task<std::shared_ptr<AccessPolicy const>>;

        /**
         * @brief Idempotently add or remove a QQ number while preserving the rest of the policy.
         * @param list List to edit.
         * @param qq Positive QQ number.
         * @param present True to add, false to remove.
         * @return Newly saved snapshot.
         * @throws std::invalid_argument If the QQ number is not positive.
         * @throws AccessConflict If another write is in flight.
         * @throws drogon::orm::DrogonDbException If saving fails.
         */
        auto SetListed(
            AccessList list,
            std::int64_t qq,
            bool present
        ) -> drogon::Task<std::shared_ptr<AccessPolicy const>>;
    };

} // namespace workflow
