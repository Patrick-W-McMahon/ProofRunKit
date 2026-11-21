/**
 * @file test_reporting.cpp
 * @brief Tests for result totals, outcome/status names, and the console and JSON reporters.
 */
#include <cstring>
#include <memory>
#include <sstream>
#include <string>
#include <vector>

#include "cpp/reporters.hpp"
#include "selftest_support.hpp"

namespace prk::selftest {

namespace {

void test_summary_adds_outcomes_without_mutating_input(PrkTestContext* context, void*) {
    const PrkRunSummary empty{0, 0, 0, 0};
    const PrkRunSummary one_pass = prk_add_outcome_to_summary(empty, PRK_OUTCOME_PASSED);
    const PrkRunSummary with_fail = prk_add_outcome_to_summary(one_pass, PRK_OUTCOME_FAILED);
    const PrkRunSummary with_skip = prk_add_outcome_to_summary(with_fail, PRK_OUTCOME_SKIPPED);
    PRK_EXPECT(empty.total == 0);
    PRK_EXPECT(one_pass.total == 1 && one_pass.passed == 1);
    PRK_EXPECT(with_skip.total == 3 && with_skip.passed == 1 && with_skip.failed == 1 && with_skip.skipped == 1);
    const PrkRunSummary unknown = prk_add_outcome_to_summary(empty, static_cast<PrkOutcome>(99));
    PRK_EXPECT(unknown.failed == 1 && unknown.passed == 0);
}

void test_outcome_and_status_names(PrkTestContext* context, void*) {
    PRK_EXPECT(std::strcmp(prk_get_outcome_name(PRK_OUTCOME_PASSED), "passed") == 0);
    PRK_EXPECT(std::strcmp(prk_get_outcome_name(PRK_OUTCOME_FAILED), "failed") == 0);
    PRK_EXPECT(std::strcmp(prk_get_outcome_name(PRK_OUTCOME_SKIPPED), "skipped") == 0);
    PRK_EXPECT(std::strcmp(prk_get_outcome_name(static_cast<PrkOutcome>(99)), "unknown") == 0);
    PRK_EXPECT(std::strcmp(prk_get_status_name(PRK_STATUS_OK), "ok") == 0);
    PRK_EXPECT(std::strcmp(prk_get_status_name(PRK_STATUS_DUPLICATE_NAME), "duplicate name") == 0);
    PRK_EXPECT(std::strcmp(prk_get_status_name(PRK_STATUS_SCRIPT_ERROR), "script error") == 0);
    PRK_EXPECT(std::strcmp(prk_get_status_name(static_cast<PrkStatus>(99)), "unknown status") == 0);
}

void test_console_lines(PrkTestContext* context, void*) {
    const RecordedTestResult passed{"core.a", PRK_OUTCOME_PASSED, "", 42};
    const RecordedTestResult failed{"core.b", PRK_OUTCOME_FAILED, "boom", 7};
    PRK_EXPECT(format_test_result_line(passed) == "[ PASS ] core.a (42 ticks)");
    PRK_EXPECT(format_test_result_line(failed) == "[ FAIL ] core.b (7 ticks): boom");
    PRK_EXPECT(format_run_summary_line(PrkRunSummary{1, 1, 0, 0}) == "1 test: 1 passed, 0 failed, 0 skipped");
    PRK_EXPECT(format_run_summary_line(PrkRunSummary{3, 1, 1, 1}) == "3 tests: 1 passed, 1 failed, 1 skipped");
}

void test_json_escaping(PrkTestContext* context, void*) {
    PRK_EXPECT(escape_json_string("plain") == "plain");
    PRK_EXPECT(escape_json_string("a\"b\\c") == "a\\\"b\\\\c");
    PRK_EXPECT(escape_json_string("line\nnext\ttab") == "line\\nnext\\ttab");
    PRK_EXPECT(escape_json_string(std::string("\x01", 1)) == "\\u0001");
}

void test_json_report_document(PrkTestContext* context, void*) {
    const std::vector<RecordedTestResult> results = {{"core.a", PRK_OUTCOME_PASSED, "", 5},
                                                     {"core.\"b\"", PRK_OUTCOME_FAILED, "bad", 6}};
    const std::string json = format_json_report(results, PrkRunSummary{2, 1, 1, 0});
    PRK_EXPECT(json.starts_with("{\n  \"schema_version\": 1,\n  \"tool\": \"proofrunkit\","));
    PRK_EXPECT(json.find("\"total\": 2, \"passed\": 1, \"failed\": 1, \"skipped\": 0") != std::string::npos);
    PRK_EXPECT(json.find("{\"name\": \"core.a\", \"outcome\": \"passed\", \"elapsed_ticks\": 5, \"message\": \"\"}") !=
               std::string::npos);
    PRK_EXPECT(json.find("\"name\": \"core.\\\"b\\\"\"") != std::string::npos);
    PRK_EXPECT(format_json_report({}, PrkRunSummary{}).find("\"tests\": []") != std::string::npos);
}

void body_that_passes(PrkTestContext*, void*) {}
void body_that_fails(PrkTestContext* context, void*) {
    prk_fail_test(context, "expected failure");
}

void test_console_reporter_writes_through_core(PrkTestContext* context, void*) {
    const std::unique_ptr<PrkRegistry, decltype(&prk_destroy_registry)> registry(prk_create_registry(),
                                                                                 &prk_destroy_registry);
    const PrkTestCase pass_case{"x.pass", &body_that_passes, nullptr};
    const PrkTestCase fail_case{"x.fail", &body_that_fails, nullptr};
    PRK_EXPECT(prk_register_test_case(registry.get(), &pass_case) == PRK_STATUS_OK);
    PRK_EXPECT(prk_register_test_case(registry.get(), &fail_case) == PRK_STATUS_OK);
    std::ostringstream output;
    ConsoleReporter console(output);
    const PrkReporter reporter = console.make_c_reporter();
    PRK_EXPECT(prk_run_test_cases(registry.get(), nullptr, &reporter, nullptr) == PRK_STATUS_OK);
    PRK_EXPECT(output.str() ==
               "[ PASS ] x.pass (0 ticks)\n[ FAIL ] x.fail (0 ticks): expected failure\n"
               "2 tests: 1 passed, 1 failed, 0 skipped\n");
}

void test_json_reporter_writes_one_document(PrkTestContext* context, void*) {
    const std::unique_ptr<PrkRegistry, decltype(&prk_destroy_registry)> registry(prk_create_registry(),
                                                                                 &prk_destroy_registry);
    const PrkTestCase pass_case{"y.pass", &body_that_passes, nullptr};
    PRK_EXPECT(prk_register_test_case(registry.get(), &pass_case) == PRK_STATUS_OK);
    std::ostringstream output;
    JsonReporter json(output);
    const PrkReporter reporter = json.make_c_reporter();
    PRK_EXPECT(prk_run_test_cases(registry.get(), nullptr, &reporter, nullptr) == PRK_STATUS_OK);
    const std::vector<RecordedTestResult> expected = {{"y.pass", PRK_OUTCOME_PASSED, "", 0}};
    PRK_EXPECT(output.str() == format_json_report(expected, PrkRunSummary{1, 1, 0, 0}));
}

}  // namespace

PrkStatus register_reporting_tests(PrkRegistry& registry) {
    const PrkTestCase test_cases[] = {
        {"core.reporting.summary_adds_outcomes_without_mutating_input",
         &test_summary_adds_outcomes_without_mutating_input, nullptr},
        {"core.reporting.outcome_and_status_names", &test_outcome_and_status_names, nullptr},
        {"cli.reporting.console_lines", &test_console_lines, nullptr},
        {"cli.reporting.json_escaping", &test_json_escaping, nullptr},
        {"cli.reporting.json_report_document", &test_json_report_document, nullptr},
        {"cli.reporting.console_reporter_writes_through_core", &test_console_reporter_writes_through_core, nullptr},
        {"cli.reporting.json_reporter_writes_one_document", &test_json_reporter_writes_one_document, nullptr},
    };
    return register_test_cases(registry, test_cases);
}

}  // namespace prk::selftest
