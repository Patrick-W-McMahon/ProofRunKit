/**
 * @file test_command.hpp
 * @brief `prk test` (#82): finds and parses prk.tests.json, resolves variables, runs the suite, and reports the
 *        combined results through the usual console / JSON / JUnit reporters.
 */
#pragma once

#include <string_view>

#include "cpp/command_line.hpp"
#include "cpp/command_line_app.hpp"
#include "test_suite.hpp"

namespace prk {

/**
 * @brief Runs `prk test` with @p options.
 * @param program_name Name shown in messages. @param options Parsed `test` options.
 * @param environment Streams, report writer, and environment.test_suite_services (required).
 * @return An ExitCode: 0, 1, 2 (usage or no config), 4 (invalid config), or 5 (report file).
 */
[[nodiscard]] int run_test_command(std::string_view program_name, const CommandLineOptions& options,
                                   const CommandLineEnvironment& environment);

/**
 * @brief Returns the real services: the system process runner, disk files, and this executable's path.
 * @return Services for main(); also safe for the self-test binary (no network).
 */
[[nodiscard]] TestSuiteServices make_system_test_suite_services();

}  // namespace prk
