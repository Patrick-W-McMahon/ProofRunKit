/**
 * @file reporters.hpp
 * @brief Console and JSON reporters that plug into the C core's PrkReporter callbacks.
 *
 * Formatting is done by pure functions; the reporter classes only collect results and write to an
 * injected std::ostream, so tests can capture output in a std::ostringstream.
 */
#pragma once

#include <cstdint>
#include <ostream>
#include <string>
#include <string_view>
#include <vector>

#include "proofrunkit/prk.h"

namespace prk {

/**
 * @brief Version of the JSON report layout (docs/report-formats.md). Bumped only for incompatible
 *        changes; new fields may be added without a bump.
 */
inline constexpr int kJsonReportSchemaVersion = 1;

/** @brief Owned copy of one PrkTestResult (the C struct is only valid during the callback). */
struct RecordedTestResult {
    std::string name;             ///< Test name.
    PrkOutcome outcome;           ///< Final outcome.
    std::string message;          ///< Failure or skip message; empty when passed.
    std::uint64_t elapsed_ticks;  ///< Ticks from the injected clock.
};

/**
 * @brief Copies a C result into an owned value. Pure function.
 * @param result Result from the core; must not be NULL.
 * @return Owned copy.
 */
[[nodiscard]] RecordedTestResult record_test_result(const PrkTestResult& result);

/**
 * @brief Formats one console line, e.g. "[ PASS ] core.registry.counts (120 ticks)". Pure function.
 * @param result Result to format.
 * @return Line without a trailing newline.
 */
[[nodiscard]] std::string format_test_result_line(const RecordedTestResult& result);

/**
 * @brief Formats the final console line, e.g. "3 tests: 2 passed, 1 failed, 0 skipped". Pure function.
 * @param summary Run totals.
 * @return Line without a trailing newline.
 */
[[nodiscard]] std::string format_run_summary_line(const PrkRunSummary& summary);

/**
 * @brief Escapes text for use inside a JSON string literal (quotes not included). Pure function.
 * @param text UTF-8 text.
 * @return Escaped text.
 */
[[nodiscard]] std::string escape_json_string(std::string_view text);

/**
 * @brief Formats a complete JSON report document. Pure function.
 * @param results Results in run order.
 * @param summary Run totals.
 * @return JSON text ending in a newline.
 */
[[nodiscard]] std::string format_json_report(const std::vector<RecordedTestResult>& results,
                                             const PrkRunSummary& summary);

/** @brief Writes one line per test and a summary line to a stream as the run progresses. */
class ConsoleReporter {
public:
    /** @brief Creates a reporter writing to @p output (not owned; must outlive the reporter). */
    explicit ConsoleReporter(std::ostream& output) : output_(output) {}

    /** @brief Returns C callbacks bound to this object for prk_run_test_cases(). */
    [[nodiscard]] PrkReporter make_c_reporter();

private:
    static void write_test_result_line(void* reporter_state, const PrkTestResult* result);
    static void write_run_summary_line(void* reporter_state, const PrkRunSummary* summary);

    std::ostream& output_;
};

/** @brief Collects results and writes one JSON document when the run finishes. */
class JsonReporter {
public:
    /** @brief Creates a reporter writing to @p output (not owned; must outlive the reporter). */
    explicit JsonReporter(std::ostream& output) : output_(output) {}

    /** @brief Returns C callbacks bound to this object for prk_run_test_cases(). */
    [[nodiscard]] PrkReporter make_c_reporter();

private:
    static void collect_test_result(void* reporter_state, const PrkTestResult* result);
    static void write_json_document(void* reporter_state, const PrkRunSummary* summary);

    std::ostream& output_;
    std::vector<RecordedTestResult> results_;
};

}  // namespace prk
