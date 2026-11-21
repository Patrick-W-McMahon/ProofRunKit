/**
 * @file test_config.hpp
 * @brief The project test configuration `prk.tests.json` read by `prk test` (#82). Pure; no IO.
 *
 * A configuration lists setup steps and test entries. Each entry either runs test scripts through a prk
 * runner (`run`) or starts any program (`command`), with its own working folder, arguments, environment,
 * expected exit code, and expected output. Strings may use `${name}` variables (see test_variables.hpp).
 * docs/test-config.md describes the schema.
 */
#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace prk {

/** @brief One NAME=VALUE environment setting (VALUE may hold variables). */
using EnvironmentSetting = std::pair<std::string, std::string>;

/** @brief One setup step or test entry. */
struct TestEntry {
    std::string name;                      ///< Unique entry name; prefixes its tests ("<name>/<test>").
    std::vector<std::string> scripts;      ///< `run`: test scripts, relative to the config folder.
    std::vector<std::string> command;      ///< `command`: program and arguments (no shell).
    std::vector<std::string> arguments;    ///< `args`: appended after the scripts or the command.
    std::string working_directory;         ///< `cwd`; "" = the config folder.
    std::vector<EnvironmentSetting> env;   ///< `env`: merged over the defaults (same name replaces).
    int expected_exit_code = 0;            ///< `expect_exit_code`.
    std::string expected_output;           ///< `expect_output`: ECMAScript regex that stdout+stderr must match.
    std::string rejected_output;           ///< `reject_output`: regex that stdout+stderr must not match.
    std::uint64_t timeout_ms = 0;          ///< `timeout_ms`; 0 = the defaults.
    bool is_template = false;              ///< `template`: expand `${name}` inside the scripts before running.
    std::optional<bool> is_report_read;    ///< `report`: "json" = read the child's JSON report; unset = auto.
    std::vector<std::string> skip_unless;  ///< `skip_unless`: paths that must exist, else the entry is skipped.
    std::vector<std::string> make_dirs;    ///< `make_dirs`: folders created before the entry starts.
};

/** @brief One config variable: a single value, or candidate paths of which the first existing one wins. */
struct TestVariable {
    std::string name;                     ///< Variable name, used as `${name}`.
    std::vector<std::string> candidates;  ///< One value, or the candidate paths in order.
    bool is_path_list = false;            ///< True when the config gave a list of candidate paths.
};

/** @brief A parsed `prk.tests.json`. */
struct TestConfig {
    std::vector<TestVariable> variables;          ///< `variables`, in file order.
    std::vector<EnvironmentSetting> default_env;  ///< `defaults.env`.
    std::uint64_t default_timeout_ms = 600000;    ///< `defaults.timeout_ms` (10 minutes unless set).
    std::string runner = "${prk}";                ///< `runner`: the prk that runs `run` entries.
    std::string work_dir;                         ///< `work_dir`; "" = the built-in default.
    std::vector<TestEntry> setup;                 ///< `setup` steps, run first, in order.
    std::vector<TestEntry> tests;                 ///< `tests`, in order.
};

/**
 * @brief Parses the text of a `prk.tests.json` document. Pure function.
 * @param text Document text.
 * @param out_error Receives "where: what" when the document is not a valid configuration.
 * @return The configuration, or std::nullopt (see @p out_error).
 */
[[nodiscard]] std::optional<TestConfig> parse_test_config(std::string_view text, std::string& out_error);

/**
 * @brief Tells whether @p name is a valid entry or variable name: letters, digits, `_`, `-`, and `.`. Pure.
 * @param name Candidate name. @return True when it is non-empty, at most 100 characters, and uses only those.
 */
[[nodiscard]] bool is_valid_test_entry_name(std::string_view name);

}  // namespace prk
