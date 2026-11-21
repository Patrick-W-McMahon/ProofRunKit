/**
 * @file junit_reporter.hpp
 * @brief JUnit XML report (#21) and a reporter fan-out so one run can feed several reports.
 *
 * The XML follows the widely consumed Jenkins/Ant JUnit layout (testsuites > testsuite > testcase with
 * failure / skipped children); see docs/report-formats.md. Formatting is pure; the reporter only
 * collects results, times them with an injected clock, and writes to an injected stream.
 */
#pragma once

#include <functional>
#include <ostream>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "proofrunkit/prk.h"
#include "reporters.hpp"

namespace prk {

/** @brief Monotonic clock in seconds, injected so report timings are testable. */
using SecondsClock = std::function<double()>;

/**
 * @brief Escapes text for XML 1.0 attribute or element content. Pure function.
 *
 * Escapes & < > " ' and replaces control characters that XML 1.0 forbids (all below U+0020 except
 * tab, newline, and carriage return) with '?'.
 * @param text UTF-8 text.
 * @return Escaped text.
 */
[[nodiscard]] std::string escape_xml_text(std::string_view text);

/**
 * @brief Splits a dotted test name into JUnit classname and name: "a.b.c" -> {"a.b", "c"}. Pure function.
 * @param test_name Registered test name.
 * @return {classname, name}; a name without a dot gets classname "proofrunkit".
 */
[[nodiscard]] std::pair<std::string, std::string> split_junit_test_name(std::string_view test_name);

/**
 * @brief Formats a complete JUnit XML document. Pure function.
 * @param results Results in run order.
 * @param elapsed_seconds Wall-clock seconds per result (same length as @p results; missing entries count as 0).
 * @param summary Run totals.
 * @return XML text ending in a newline.
 */
[[nodiscard]] std::string format_junit_report(const std::vector<RecordedTestResult>& results,
                                              const std::vector<double>& elapsed_seconds, const PrkRunSummary& summary);

/** @brief Returns a SecondsClock backed by std::chrono::steady_clock. */
[[nodiscard]] SecondsClock make_steady_seconds_clock();

/** @brief Collects results and writes one JUnit XML document when the run finishes. */
class JUnitReporter {
public:
    /**
     * @param output Stream for the document (not owned; must outlive the reporter).
     * @param clock Clock for per-test wall time; an empty function records 0 seconds.
     */
    JUnitReporter(std::ostream& output, SecondsClock clock) : output_(output), clock_(std::move(clock)) {}

    /** @brief Returns C callbacks bound to this object for prk_run_test_cases(). */
    [[nodiscard]] PrkReporter make_c_reporter();

private:
    static void start_run(void* reporter_state, std::size_t selected_test_count);
    static void collect_test_result(void* reporter_state, const PrkTestResult* result);
    static void write_junit_document(void* reporter_state, const PrkRunSummary* summary);
    [[nodiscard]] double read_clock() const { return clock_ ? clock_() : 0.0; }

    std::ostream& output_;
    SecondsClock clock_;
    double last_finish_seconds_ = 0.0;
    std::vector<RecordedTestResult> results_;
    std::vector<double> elapsed_seconds_;
};

/** @brief Forwards every callback to several reporters, in order. */
class ReporterFanOut {
public:
    /** @brief Adds a reporter; its state must outlive the fan-out. */
    void add_reporter(const PrkReporter& reporter) { reporters_.push_back(reporter); }

    /** @brief Returns C callbacks bound to this object for prk_run_test_cases(). */
    [[nodiscard]] PrkReporter make_c_reporter();

private:
    static void forward_run_started(void* reporter_state, std::size_t selected_test_count);
    static void forward_test_finished(void* reporter_state, const PrkTestResult* result);
    static void forward_run_finished(void* reporter_state, const PrkRunSummary* summary);

    std::vector<PrkReporter> reporters_;
};

}  // namespace prk
