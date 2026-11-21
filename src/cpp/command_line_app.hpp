/**
 * @file command_line_app.hpp
 * @brief Shared front end used by both `proofrunkit` and `proofrunkit_selftest`.
 */
#pragma once

#include <ostream>
#include <span>
#include <string_view>

#include "proofrunkit/prk.h"
#include "script_host.hpp"
#include "script_loading.hpp"

namespace prk {

/** @brief Process exit codes returned by run_command_line(). */
enum ExitCode : int {
    kExitAllPassed = 0,       ///< Every selected test passed (or nothing needed running).
    kExitTestFailed = 1,      ///< At least one selected test failed.
    kExitUsageError = 2,      ///< The command line was invalid.
    kExitNotImplemented = 3,  ///< A requested feature (e.g. JavaScript scripts, #11) has not landed yet.
    kExitScriptError = 4,     ///< A test script could not be read, compiled, or loaded (#9).
};

/** @brief Injected dependencies of run_command_line(). */
struct CommandLineEnvironment {
    const PrkRegistry& builtin_registry;  ///< Tests run when no scripts are given (built-in checks).
    PrkClock clock;                       ///< Clock used to time test bodies.
    ScriptFileReader read_script_file;    ///< Reads test scripts named on the command line.
    ScriptHostServices script_services;   ///< Clock and read-only files granted to test scripts (#12).
    std::ostream& output;                 ///< Results, listings, help, version, and console-mode print().
    std::ostream& errors;                 ///< Usage errors, script load errors, and JSON-mode print().
};

/**
 * @brief Parses @p arguments and lists or runs tests, writing to the environment's streams.
 *
 * With script paths, only the tests those scripts register are listed or run; otherwise the
 * built-in registry is used. All IO and the clock are injected, so tests can drive this with
 * string streams, in-memory scripts, and a fake clock.
 *
 * @param program_name Name shown in usage text.
 * @param arguments Command-line arguments without the program name.
 * @param environment Registry, clock, script reader, and streams.
 * @return One of the ExitCode values.
 */
[[nodiscard]] int run_command_line(std::string_view program_name, std::span<const std::string_view> arguments,
                                   const CommandLineEnvironment& environment);

/**
 * @brief Returns a clock that reads prk_read_cycle_counter(), the Assembly hot path where available.
 * @return Clock suitable for PrkRunOptions.
 */
[[nodiscard]] PrkClock make_cycle_counter_clock();

}  // namespace prk
