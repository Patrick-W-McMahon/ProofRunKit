/**
 * @file test_reporting.cpp
 * @brief Internal tests for result totals and outcome/status names.
 *        Cases that prk scripts cover are in prk.tests.json (docs/self-hosting.md, #96).
 */
#include <cstring>

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

}  // namespace

PrkStatus register_reporting_tests(PrkRegistry& registry) {
    const PrkTestCase test_cases[] = {
        {"core.reporting.summary_adds_outcomes_without_mutating_input",
         &test_summary_adds_outcomes_without_mutating_input, nullptr},
        {"core.reporting.outcome_and_status_names", &test_outcome_and_status_names, nullptr},
    };
    return register_test_cases(registry, test_cases);
}

}  // namespace prk::selftest
