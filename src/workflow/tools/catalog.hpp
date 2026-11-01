/**
 * @file catalog.hpp
 * @brief Declare stable built-in tool definitions shared by validation, execution, and the editor.
 */
#pragma once
#include <json/value.h>

namespace tools {
    /**
     * @brief Return deterministic built-in names, descriptions, schemas, and group restrictions.
     * @return Complete tool catalog.
     */
    auto builtin_catalog() -> Json::Value;
    /**
     * @brief Test whether a name belongs to the built-in catalog.
     * @param name Candidate tool name.
     * @return Whether custom tools must avoid the name.
     */
    auto is_builtin(std::string const& name) -> bool;
} // namespace tools
