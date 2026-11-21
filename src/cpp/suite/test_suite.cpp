/**
 * @file test_suite.cpp
 * @brief Runs the setup steps and test entries of a `prk.tests.json` (#82) and reads child JSON reports.
 */
#include "test_suite.hpp"

#include "cpp/update/json_reader.hpp"

namespace prk {

namespace {

/** @brief Maps a report outcome name to PrkOutcome; failed for anything unknown. */
PrkOutcome parse_outcome_name(const std::string& name) {
    if (name == "passed") {
        return PRK_OUTCOME_PASSED;
    }
    return name == "skipped" ? PRK_OUTCOME_SKIPPED : PRK_OUTCOME_FAILED;
}

/** @brief Counts results by outcome for the progress line. */
std::string summarize_entry_results(const std::vector<SuiteTestResult>& results) {
    std::size_t failed = 0;
    std::size_t skipped = 0;
    std::uint64_t elapsed_ms = 0;
    for (const SuiteTestResult& result : results) {
        failed += result.outcome == PRK_OUTCOME_FAILED ? 1 : 0;
        skipped += result.outcome == PRK_OUTCOME_SKIPPED ? 1 : 0;
        elapsed_ms += result.elapsed_ms;
    }
    const std::string status = failed > 0 ? "FAIL" : (skipped == results.size() ? "SKIP" : "PASS");
    std::string line = "[ " + status + " ] " + std::to_string(results.size()) + " result(s)";
    if (failed > 0 || skipped > 0) {
        line += ", " + std::to_string(failed) + " failed, " + std::to_string(skipped) + " skipped";
    }
    return line + (elapsed_ms > 0 ? ", " + std::to_string(elapsed_ms) + " ms" : std::string());
}

/** @brief Writes the first failure or skip message of @p results, indented, for the progress stream. */
void write_first_problem(const std::vector<SuiteTestResult>& results, std::ostream& progress) {
    for (const SuiteTestResult& result : results) {
        if (result.outcome != PRK_OUTCOME_PASSED) {
            progress << "         " << result.name << ": " << result.message << '\n';
            return;
        }
    }
}

}  // namespace

std::vector<SuiteTestResult> read_suite_entry_report(const std::string& entry_name, const std::string& report_path,
                                                     const TestSuiteServices& services, std::string& out_problem) {
    out_problem.clear();
    const std::optional<std::string> text = services.read_file ? services.read_file(report_path) : std::nullopt;
    if (!text || text->empty()) {
        out_problem = "no JSON report at " + report_path;
        return {};
    }
    std::string json_error;
    const std::optional<JsonValue> report = parse_json_document(*text, json_error);
    const JsonValue* tests = report ? report->find_member("tests") : nullptr;
    if (tests == nullptr || tests->kind != JsonValue::Kind::Array) {
        out_problem = "invalid JSON report at " + report_path;
        return {};
    }
    std::vector<SuiteTestResult> results;
    for (const JsonValue& test : tests->items) {
        results.push_back(SuiteTestResult{entry_name + "/" + test.read_string_member("name"),
                                          parse_outcome_name(test.read_string_member("outcome")),
                                          test.read_string_member("message"), 0});
    }
    return results;
}

std::vector<SuiteTestResult> run_test_suite(const TestConfig& config, const SuiteRunSettings& settings,
                                            const TestSuiteServices& services, std::ostream& progress) {
    std::vector<SuiteTestResult> combined;
    for (const TestEntry& step : config.setup) {
        progress << "setup " << step.name << " ... " << std::flush;
        std::vector<SuiteTestResult> results = run_test_entry(step, config, settings, services);
        progress << summarize_entry_results(results) << '\n';
        write_first_problem(results, progress);
        for (SuiteTestResult& result : results) {
            if (result.outcome == PRK_OUTCOME_FAILED) {
                combined.push_back(SuiteTestResult{"setup/" + step.name, PRK_OUTCOME_FAILED,
                                                   result.name + ": " + result.message, result.elapsed_ms});
                return combined;  // Later entries would only fail for the same reason.
            }
        }
    }
    for (const TestEntry& entry : config.tests) {
        if (!settings.name_filter.empty() && entry.name.find(settings.name_filter) == std::string::npos) {
            continue;
        }
        progress << entry.name << " ... " << std::flush;
        std::vector<SuiteTestResult> results = run_test_entry(entry, config, settings, services);
        progress << summarize_entry_results(results) << '\n';
        write_first_problem(results, progress);
        combined.insert(combined.end(), results.begin(), results.end());
    }
    return combined;
}

}  // namespace prk
