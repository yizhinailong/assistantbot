/**
 * @file access_control.cpp
 * @brief Implement persistent friend access policies and immutable snapshots.
 */
#include "workflow/access_control.hpp"

#include <algorithm>
#include <charconv>
#include <string>
#include <utility>

#include <json/value.h>

#include "storage/settings.hpp"

namespace workflow {

    namespace {

        /**
         * @brief Encode a whole policy in the settings table's JSON format.
         * @param policy Policy to save.
         * @return Mode and sorted lists of decimal QQ strings.
         */
        auto serialize(AccessPolicy const& policy) -> Json::Value {
            Json::Value body{ Json::objectValue };
            body["mode"] = access_mode_name(policy.mode);
            body["allowlist"] = Json::Value{ Json::arrayValue };
            body["blacklist"] = Json::Value{ Json::arrayValue };
            for (auto const qq : policy.allowlist) {
                body["allowlist"].append(std::to_string(qq));
            }
            for (auto const qq : policy.blacklist) {
                body["blacklist"].append(std::to_string(qq));
            }
            return body;
        }

        /**
         * @brief Validate and decode a policy saved in SQLite.
         * @param body Saved JSON value.
         * @return Complete policy.
         * @throws std::invalid_argument If any field is missing or invalid.
         */
        auto deserialize(Json::Value const& body) -> AccessPolicy {
            if (!body.isObject() || body.size() != 3 || !body["mode"].isString() || !body["allowlist"].isArray() || !body["blacklist"].isArray()) {
                throw std::invalid_argument{ "access policy must contain mode, allowlist, and blacklist" };
            }
            AccessPolicy policy{ .mode = parse_access_mode(body["mode"].asString()) };
            for (auto const* name : { "allowlist", "blacklist" }) {
                auto& list{ std::string_view{ name } == "allowlist" ? policy.allowlist : policy.blacklist };
                for (auto const& qq : body[name]) {
                    if (!qq.isString()) {
                        throw std::invalid_argument{ "access policy QQ numbers must be decimal strings" };
                    }
                    list.insert(parse_qq(qq.asString()));
                }
            }
            return policy;
        }

    } // namespace

    /**
     * @brief Parse a positive decimal QQ number without losing integer precision.
     * @param text Decimal digits, with no sign or whitespace.
     * @return QQ number in the signed 64-bit range.
     * @throws std::invalid_argument If the number is invalid or out of range.
     */
    auto parse_qq(std::string_view text) -> std::int64_t {
        if (text.empty() || !std::ranges::all_of(text, [](char ch) { return ch >= '0' && ch <= '9'; })) {
            throw std::invalid_argument{ "qq must be a positive decimal string in the signed 64-bit range" };
        }
        std::int64_t qq{};
        auto const [end, error]{ std::from_chars(text.data(), text.data() + text.size(), qq) };
        if (error != std::errc{} || end != text.data() + text.size() || qq <= 0) {
            throw std::invalid_argument{ "qq must be a positive decimal string in the signed 64-bit range" };
        }
        return qq;
    }

    /**
     * @brief Parse an administrative access mode name.
     * @param name Either `all_friends` or `allowlist`.
     * @return Corresponding mode.
     * @throws std::invalid_argument If the name is unknown.
     */
    auto parse_access_mode(std::string_view name) -> AccessMode {
        if (name == "all_friends") {
            return AccessMode::AllFriends;
        }
        if (name == "allowlist") {
            return AccessMode::Allowlist;
        }
        throw std::invalid_argument{ "mode must be all_friends or allowlist" };
    }

    /**
     * @brief Get the JSON name of an access mode.
     * @param mode Mode to serialize.
     * @return `all_friends` or `allowlist`.
     */
    auto access_mode_name(AccessMode mode) -> char const* {
        return mode == AccessMode::AllFriends ? "all_friends" : "allowlist";
    }

    /**
     * @brief Decide whether a friend's new message may reach commands or the queue.
     * @param qq Sender's QQ number.
     * @return True when the policy permits a reply.
     */
    auto AccessPolicy::Allows(std::int64_t qq) const -> bool {
        return !blacklist.contains(qq) && (mode == AccessMode::AllFriends || allowlist.contains(qq));
    }

    /**
     * @brief Read the saved policy, or default to all friends with empty lists.
     * @param db Database with the settings table.
     * @throws std::invalid_argument If the saved policy is invalid.
     * @throws std::runtime_error If the saved JSON is malformed.
     * @throws drogon::orm::DrogonDbException If reading fails.
     * @note Blocks; call only before event loops start, during plugin initialization.
     */
    AccessControl::AccessControl(drogon::orm::DbClientPtr db)
        : m_db{ std::move(db) },
          m_current{ std::make_shared<AccessPolicy const>() } {
        if (auto const saved{ storage::load_setting(*m_db, ACCESS_SETTINGS_NAME) }) {
            try {
                m_current = std::make_shared<AccessPolicy const>(deserialize(*saved));
            } catch (std::invalid_argument const& error) {
                throw std::invalid_argument{ std::string{ "controllers::AssistantBot: invalid access policy saved in the database: " } + error.what() };
            }
        }
    }

    /**
     * @brief Get the policy in effect without querying storage.
     * @return Immutable snapshot unaffected by later changes.
     */
    auto AccessControl::Current() const -> std::shared_ptr<AccessPolicy const> {
        std::lock_guard lock{ m_mutex };
        return m_current;
    }

    /**
     * @brief Copy, mutate, save, and publish one policy under an exclusive write claim.
     * @param change Mutation applied to a private copy of the current policy.
     * @return Newly saved snapshot.
     * @throws AccessConflict If another write is in flight.
     * @throws drogon::orm::DrogonDbException If saving fails; the snapshot stays unchanged.
     */
    auto AccessControl::update(
        std::function<void(AccessPolicy&)> change
    ) -> drogon::Task<std::shared_ptr<AccessPolicy const>> {
        std::shared_ptr<AccessPolicy> next{};
        {
            std::lock_guard lock{ m_mutex };
            if (m_saving) {
                throw AccessConflict{ "another access policy save is still running; retry the change" };
            }
            next = std::make_shared<AccessPolicy>(*m_current);
            change(*next);
            m_saving = true;
        }
        try {
            co_await storage::save_setting(m_db, ACCESS_SETTINGS_NAME, serialize(*next));
        } catch (...) {
            std::lock_guard lock{ m_mutex };
            m_saving = false;
            throw;
        }
        std::lock_guard lock{ m_mutex };
        m_current = std::move(next);
        m_saving = false;
        co_return m_current;
    }

    /**
     * @brief Save the mode while preserving both lists.
     * @param mode New access mode.
     * @return Newly saved snapshot.
     * @throws AccessConflict If another write is in flight.
     * @throws drogon::orm::DrogonDbException If saving fails.
     */
    auto AccessControl::SetMode(
        AccessMode mode
    ) -> drogon::Task<std::shared_ptr<AccessPolicy const>> {
        return update([mode](AccessPolicy& policy) { policy.mode = mode; });
    }

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
    auto AccessControl::SetListed(
        AccessList list,
        std::int64_t qq,
        bool present
    ) -> drogon::Task<std::shared_ptr<AccessPolicy const>> {
        if (qq <= 0) {
            throw std::invalid_argument{ "qq must be positive" };
        }
        return update([list, qq, present](AccessPolicy& policy) {
            auto& entries{ list == AccessList::Allowlist ? policy.allowlist : policy.blacklist };
            if (present) {
                entries.insert(qq);
            } else {
                entries.erase(qq);
            }
        });
    }

} // namespace workflow
