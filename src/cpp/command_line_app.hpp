/**
 * @file command_line_app.hpp
 * @brief Shared front end used by both `proofrunkit` and `proofrunkit_selftest`.
 */
#pragma once

#include <functional>
#include <ostream>
#include <span>
#include <string>
#include <string_view>

#include "command_line.hpp"
#include "proofrunkit/prk.h"
#include "script_host.hpp"
#include "script_loading.hpp"
#include "update/update_command.hpp"

namespace prk {
struct TestSuiteServices;
}  // namespace prk

namespace prk {

/** @brief Process exit codes returned by run_command_line(). */
enum ExitCode : int {
    kExitAllPassed = 0,       ///< Every selected test passed (or nothing needed running).
    kExitTestFailed = 1,      ///< At least one selected test failed.
    kExitUsageError = 2,      ///< The command line was invalid.
    kExitNotImplemented = 3,  ///< Reserved: a requested feature has not landed yet (unused since #11).
    kExitScriptError = 4,     ///< A test script could not be read, compiled, or loaded (#9).
    kExitReportError = 5,     ///< A requested report file could not be written (#21).
    kExitUpdateFailed = 6,    ///< `prk -update` failed: no release, network, checksum, or platform (#75).
};
static_assert(kExitUpdateFailed == kUpdateFailedExitCode, "update exit codes must agree");

/**
 * @brief Writes a whole report file. Returns an empty string on success, otherwise the reason it failed.
 *        Injected so tests can capture report files in memory.
 */
using ReportFileWriter = std::function<std::string(const std::string& path, const std::string& text)>;

/**
 * @brief Writes @p text to @p path in binary mode, replacing the file. The IO edge used by main().
 * @return Empty on success, otherwise a message such as "cannot open file for writing".
 */
[[nodiscard]] std::string write_report_file_to_disk(const std::string& path, const std::string& text);

/** @brief Injected dependencies of run_command_line(). */
struct CommandLineEnvironment {
    const PrkRegistry& builtin_registry;    ///< Tests run when no scripts are given (built-in checks).
    PrkClock clock;                         ///< Clock used to time test bodies.
    ScriptFileReader read_script_file;      ///< Reads test scripts named on the command line.
    ScriptHostServices script_services;     ///< Clock and read-only files granted to test scripts (#12).
    ReportFileWriter write_report_file;     ///< Writes --json-file / --junit-file reports (#21).
    std::ostream& output;                   ///< Results, listings, help, version, and console-mode print().
    std::ostream& errors;                   ///< Usage errors, script load errors, and JSON-mode print().
    const UpdateServices* update_services;  ///< Release lookup and installer for -version / -update (#75); nullptr =
                                            ///< offline (no network: the self-test, CTest, PRK_OFFLINE=1).
    const TestSuiteServices* test_suite_services;  ///< Processes and files for `prk test` (#82); nullptr = none.
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
 * @brief Runs every test in @p registry with the stdout reporter selected by @p options plus any report files.
 *        Also used by `prk test` (#82) to report its combined results.
 * @param program_name Name shown in errors. @param options Report format and report files.
 * @param registry Tests to run. @param environment Clock, streams, and report writer.
 * @return kExitAllPassed, kExitTestFailed, or kExitReportError.
 */
[[nodiscard]] int run_selected_tests(std::string_view program_name, const CommandLineOptions& options,
                                     const PrkRegistry& registry, const CommandLineEnvironment& environment);

/**
 * @brief Returns a clock that reads prk_read_cycle_counter(), the Assembly hot path where available.
 * @return Clock suitable for PrkRunOptions.
 */
[[nodiscard]] PrkClock make_cycle_counter_clock();

}  // namespace prk
