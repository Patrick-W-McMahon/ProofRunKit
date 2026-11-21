/**
 * @file test_variables.hpp
 * @brief `${name}` variables of `prk.tests.json` (#82): expansion and resolution. Pure; paths are checked
 *        through an injected function.
 */
#pragma once

#include <functional>
#include <map>
#include <optional>
#include <string>
#include <string_view>

#include "test_config.hpp"

namespace prk {

/** @brief Variable values by name. */
using TestVariables = std::map<std::string, std::string, std::less<>>;

/** @brief Tells whether a file or folder exists. */
using PathExistsFunction = std::function<bool(const std::string& path)>;

/**
 * @brief Replaces every `${name}` in @p text; `$${` stays as a literal `${`. Pure function.
 * @param text Text with variables. @param variables Known values.
 * @param out_missing Receives the first unknown or malformed variable name when expansion fails.
 * @return The expanded text, or std::nullopt when a variable is unknown or a `${` is not closed.
 */
[[nodiscard]] std::optional<std::string> expand_test_variables(std::string_view text, const TestVariables& variables,
                                                               std::string& out_missing);

/**
 * @brief Resolves the config's variables on top of @p builtins.
 *
 * Values in @p overrides (from `--var`) win over everything. A single-value variable takes its expanded value.
 * A list takes the first candidate that expands and exists according to @p path_exists. Variables may refer to
 * each other in any order; a variable whose references never resolve stays unset (entries that use it are
 * skipped).
 *
 * @param config Parsed configuration. @param builtins Built-in values such as prk, config_dir, build_dir.
 * @param overrides `--var NAME=VALUE` values. @param path_exists Existence check for list candidates.
 * @return All resolved values.
 */
[[nodiscard]] TestVariables resolve_test_variables(const TestConfig& config, const TestVariables& builtins,
                                                   const TestVariables& overrides,
                                                   const PathExistsFunction& path_exists);

}  // namespace prk
