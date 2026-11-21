/**
 * @file test_suite.hpp
 * @brief Runs a parsed `prk.tests.json` (#82): setup steps, then test entries, each as one child process
 *        through an injected PrkProcessRunner, collecting one combined list of results.
 */
#pragma once

#include <cstdint>
#include <functional>
#include <optional>
#include <ostream>
#include <string>
#include <vector>

#include "proofrunkit/prk.h"
#include "proofrunkit/prk_process.h"
#include "test_config.hpp"
#include "test_variables.hpp"

namespace prk {

/** @brief IO used by `prk test`; injected so self-tests use fakes. */
struct TestSuiteServices {
    PrkProcessRunner process_runner{};                                             ///< Starts entries.
    std::function<std::optional<std::string>(const std::string& path)> read_file;  ///< Whole file, or nullopt.
    std::function<std::string(const std::string& path, const std::string& text)> write_file;  ///< "" or why not;
                                                                                              ///< makes folders.
    PathExistsFunction path_exists;                                          ///< File or folder exists.
    std::function<std::string()> find_own_executable;                        ///< Absolute path of the running prk.
    std::function<std::string(const std::string& path)> make_absolute_path;  ///< Absolute, normalised path.
    char path_separator = '/';                                               ///< '\\' on Windows.
    std::string executable_suffix;                                           ///< ".exe" on Windows, else "".
    std::string os_name = "linux";                                           ///< "windows", "linux", or "macos".
};

/** @brief One result in the combined run. */
struct SuiteTestResult {
    std::string name;                         ///< "<entry>" or "<entry>/<test>".
    PrkOutcome outcome = PRK_OUTCOME_PASSED;  ///< Final outcome.
    std::string message;                      ///< Failure or skip reason; "" when passed.
    std::uint64_t elapsed_ms = 0;             ///< Wall-clock time (the whole entry for entry results).
};

/** @brief Resolved values a run needs. */
struct SuiteRunSettings {
    TestVariables variables;  ///< Resolved variables (built-ins, config, overrides).
    std::string config_dir;   ///< Absolute folder of the config; relative paths start here.
    std::string work_dir;     ///< Absolute folder for templates and child reports.
    std::string name_filter;  ///< Run only test entries whose name contains this; setup always runs.
};

/**
 * @brief Runs @p config: every setup step (stopping at the first failure), then every selected test entry.
 * @param config Parsed configuration. @param settings Resolved variables and folders.
 * @param services Process runner and file access. @param progress Receives one line per entry.
 * @return Combined results in order; each entry adds its own tests ("<entry>/<test>") and/or one entry result.
 */
[[nodiscard]] std::vector<SuiteTestResult> run_test_suite(const TestConfig& config, const SuiteRunSettings& settings,
                                                          const TestSuiteServices& services, std::ostream& progress);

/**
 * @brief Runs one entry and evaluates its exit code, output, and (when read) its JSON report.
 * @param entry Entry to run. @param config Its configuration (runner, defaults). @param settings Resolved values.
 * @param services Process runner and file access.
 * @return The entry's results: one entry result, and/or one result per test in its JSON report.
 */
[[nodiscard]] std::vector<SuiteTestResult> run_test_entry(const TestEntry& entry, const TestConfig& config,
                                                          const SuiteRunSettings& settings,
                                                          const TestSuiteServices& services);

/**
 * @brief Reads the JSON report a child wrote with `--json-file` and names its tests "<entry>/<test>".
 * @param entry_name Entry name used as the prefix. @param report_path Report file.
 * @param services File access. @param out_problem Receives why the report is missing or invalid, else "".
 * @return One result per test in the report (elapsed 0: report ticks are not milliseconds).
 */
[[nodiscard]] std::vector<SuiteTestResult> read_suite_entry_report(const std::string& entry_name,
                                                                   const std::string& report_path,
                                                                   const TestSuiteServices& services,
                                                                   std::string& out_problem);

/**
 * @brief Joins @p path to @p base unless it is already absolute (`/x`, `\x`, or `C:...`). Pure function.
 * @param base Folder. @param path Relative or absolute path. @param separator Separator to insert.
 * @return The joined path, or @p path itself when absolute or @p base is empty.
 */
[[nodiscard]] std::string join_suite_path(const std::string& base, const std::string& path, char separator);

}  // namespace prk
