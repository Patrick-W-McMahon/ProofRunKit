/**
 * @file command_line_app.hpp
 * @brief Shared front end used by both `proofrunkit` and `proofrunkit_selftest`.
 */
#pragma once

#include <ostream>
#include <span>
#include <string_view>

#include "proofrunkit/prk.h"

namespace prk {

/** @brief Process exit codes returned by run_command_line(). */
enum ExitCode : int {
    kExitAllPassed = 0,       ///< Every selected test passed (or nothing needed running).
    kExitTestFailed = 1,      ///< At least one selected test failed.
    kExitUsageError = 2,      ///< The command line was invalid.
    kExitNotImplemented = 3,  ///< A requested feature (e.g. Lua/JS scripts) has not landed yet.
};

/**
 * @brief Parses @p arguments and lists or runs the tests in @p registry, writing to the given streams.
 *
 * All IO and the clock are injected, so tests can drive this with string streams and a fake clock.
 *
 * @param program_name Name shown in usage text.
 * @param arguments Command-line arguments without the program name.
 * @param registry Test cases available to run.
 * @param clock Clock used to time test bodies.
 * @param output Stream for results, listings, help, and version text.
 * @param errors Stream for usage errors and not-implemented notices.
 * @return One of the ExitCode values.
 */
[[nodiscard]] int run_command_line(std::string_view program_name, std::span<const std::string_view> arguments,
                                   const PrkRegistry& registry, const PrkClock& clock, std::ostream& output,
                                   std::ostream& errors);

/**
 * @brief Returns a clock that reads prk_read_cycle_counter(), the Assembly hot path where available.
 * @return Clock suitable for PrkRunOptions.
 */
[[nodiscard]] PrkClock make_cycle_counter_clock();

}  // namespace prk
