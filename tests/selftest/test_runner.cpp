/**
 * @file test_runner.cpp
 * @brief Tests for prk_run_test_cases, prk_fail_test, prk_skip_test, and prk_is_test_name_selected.
 */
#include <cstdint>
#include <cstring>
#include <memory>
#include <string>
#include <vector>

#include "cpp/reporters.hpp"
#include "selftest_support.hpp"

namespace prk::selftest {

namespace {

using RegistryPointer = std::unique_ptr<PrkRegistry, decltype(&prk_destroy_registry)>;

RegistryPointer create_owned_registry() {
    return RegistryPointer(prk_create_registry(), &prk_destroy_registry);
}

/** @brief Fake clock: every read advances by 10 ticks. */
std::uint64_t read_fake_clock_ticks(void* clock_state) {
    auto* const ticks = static_cast<std::uint64_t*>(clock_state);
    *ticks += 10;
    return *ticks;
}

/** @brief Reporter state that records every callback. */
struct RecordingReporterState {
    std::size_t announced_count = 0;
    std::vector<RecordedTestResult> results;
    PrkRunSummary final_summary{};
    int finished_calls = 0;
};

void record_run_started(void* state, std::size_t count) {
    static_cast<RecordingReporterState*>(state)->announced_count = count;
}
void record_test_finished(void* state, const PrkTestResult* result) {
    static_cast<RecordingReporterState*>(state)->results.push_back(record_test_result(*result));
}
void record_run_finished(void* state, const PrkRunSummary* summary) {
    auto* const recording = static_cast<RecordingReporterState*>(state);
    recording->final_summary = *summary;
    recording->finished_calls += 1;
}

PrkReporter make_recording_reporter(RecordingReporterState& state) {
    return PrkReporter{&record_run_started, &record_test_finished, &record_run_finished, &state};
}

void body_that_passes(PrkTestContext*, void*) {}
void body_that_fails(PrkTestContext* context, void*) {
    prk_fail_test(context, "first failure");
}
void body_that_skips(PrkTestContext* context, void*) {
    prk_skip_test(context, "not on this host");
}
void body_that_fails_twice(PrkTestContext* context, void*) {
    prk_fail_test(context, "kept");
    prk_fail_test(context, "ignored");
}
void body_that_fails_then_skips(PrkTestContext* context, void*) {
    prk_fail_test(context, "failure wins");
    prk_skip_test(context, "too late");
}
void body_that_skips_then_fails(PrkTestContext* context, void*) {
    prk_skip_test(context, "skip");
    prk_fail_test(context, "failure overrides skip");
}
void body_with_long_message(PrkTestContext* context, void*) {
    prk_fail_test(context, std::string(2000, 'x').c_str());
}
void body_that_counts_calls(PrkTestContext*, void* user_state) {
    *static_cast<int*>(user_state) += 1;
}

PrkStatus register_named(PrkRegistry& registry, const char* name, PrkTestBody body, void* user_state = nullptr) {
    const PrkTestCase test_case{name, body, user_state};
    return prk_register_test_case(&registry, &test_case);
}

void test_runs_in_order_and_counts_outcomes(PrkTestContext* context, void*) {
    const RegistryPointer registry = create_owned_registry();
    PRK_EXPECT(register_named(*registry, "a.pass", &body_that_passes) == PRK_STATUS_OK);
    PRK_EXPECT(register_named(*registry, "b.fail", &body_that_fails) == PRK_STATUS_OK);
    PRK_EXPECT(register_named(*registry, "c.skip", &body_that_skips) == PRK_STATUS_OK);
    RecordingReporterState recording;
    const PrkReporter reporter = make_recording_reporter(recording);
    PrkRunSummary summary{};
    PRK_EXPECT(prk_run_test_cases(registry.get(), nullptr, &reporter, &summary) == PRK_STATUS_OK);
    PRK_EXPECT(recording.announced_count == 3);
    PRK_EXPECT(recording.finished_calls == 1);
    PRK_EXPECT(recording.results.size() == 3);
    PRK_EXPECT(recording.results[0].name == "a.pass" && recording.results[0].outcome == PRK_OUTCOME_PASSED);
    PRK_EXPECT(recording.results[1].outcome == PRK_OUTCOME_FAILED && recording.results[1].message == "first failure");
    PRK_EXPECT(recording.results[2].outcome == PRK_OUTCOME_SKIPPED);
    PRK_EXPECT(summary.total == 3 && summary.passed == 1 && summary.failed == 1 && summary.skipped == 1);
    PRK_EXPECT(recording.final_summary.total == 3);
}

void test_filter_selects_matching_names(PrkTestContext* context, void*) {
    const RegistryPointer registry = create_owned_registry();
    PRK_EXPECT(register_named(*registry, "core.one", &body_that_passes) == PRK_STATUS_OK);
    PRK_EXPECT(register_named(*registry, "cli.two", &body_that_fails) == PRK_STATUS_OK);
    RecordingReporterState recording;
    const PrkReporter reporter = make_recording_reporter(recording);
    const PrkRunOptions options{"core.", PrkClock{nullptr, nullptr}};
    PrkRunSummary summary{};
    PRK_EXPECT(prk_run_test_cases(registry.get(), &options, &reporter, &summary) == PRK_STATUS_OK);
    PRK_EXPECT(recording.announced_count == 1);
    PRK_EXPECT(summary.total == 1 && summary.passed == 1 && summary.failed == 0);
}

void test_uses_injected_clock_for_elapsed_ticks(PrkTestContext* context, void*) {
    const RegistryPointer registry = create_owned_registry();
    PRK_EXPECT(register_named(*registry, "timed", &body_that_passes) == PRK_STATUS_OK);
    std::uint64_t fake_ticks = 0;
    RecordingReporterState recording;
    const PrkReporter reporter = make_recording_reporter(recording);
    const PrkRunOptions options{nullptr, PrkClock{&read_fake_clock_ticks, &fake_ticks}};
    PRK_EXPECT(prk_run_test_cases(registry.get(), &options, &reporter, nullptr) == PRK_STATUS_OK);
    PRK_EXPECT(recording.results.size() == 1 && recording.results[0].elapsed_ticks == 10);
}

void test_first_failure_message_is_kept(PrkTestContext* context, void*) {
    const RegistryPointer registry = create_owned_registry();
    PRK_EXPECT(register_named(*registry, "twice", &body_that_fails_twice) == PRK_STATUS_OK);
    PRK_EXPECT(register_named(*registry, "fail_then_skip", &body_that_fails_then_skips) == PRK_STATUS_OK);
    PRK_EXPECT(register_named(*registry, "skip_then_fail", &body_that_skips_then_fails) == PRK_STATUS_OK);
    RecordingReporterState recording;
    const PrkReporter reporter = make_recording_reporter(recording);
    PRK_EXPECT(prk_run_test_cases(registry.get(), nullptr, &reporter, nullptr) == PRK_STATUS_OK);
    PRK_EXPECT(recording.results.size() == 3);
    PRK_EXPECT(recording.results[0].message == "kept");
    PRK_EXPECT(recording.results[1].outcome == PRK_OUTCOME_FAILED && recording.results[1].message == "failure wins");
    PRK_EXPECT(recording.results[2].outcome == PRK_OUTCOME_FAILED);
    PRK_EXPECT(recording.results[2].message == "failure overrides skip");
}

void test_long_messages_are_truncated(PrkTestContext* context, void*) {
    const RegistryPointer registry = create_owned_registry();
    PRK_EXPECT(register_named(*registry, "long", &body_with_long_message) == PRK_STATUS_OK);
    RecordingReporterState recording;
    const PrkReporter reporter = make_recording_reporter(recording);
    PRK_EXPECT(prk_run_test_cases(registry.get(), nullptr, &reporter, nullptr) == PRK_STATUS_OK);
    PRK_EXPECT(recording.results.size() == 1);
    PRK_EXPECT(recording.results[0].message.size() == PRK_MESSAGE_CAPACITY - 1);
}

void test_passes_user_state_and_runs_silently(PrkTestContext* context, void*) {
    const RegistryPointer registry = create_owned_registry();
    int call_count = 0;
    PRK_EXPECT(register_named(*registry, "counter", &body_that_counts_calls, &call_count) == PRK_STATUS_OK);
    PRK_EXPECT(prk_run_test_cases(registry.get(), nullptr, nullptr, nullptr) == PRK_STATUS_OK);
    PRK_EXPECT(call_count == 1);
}

void test_rejects_null_registry(PrkTestContext* context, void*) {
    PrkRunSummary summary{};
    PRK_EXPECT(prk_run_test_cases(nullptr, nullptr, nullptr, &summary) == PRK_STATUS_INVALID_ARGUMENT);
    prk_fail_test(nullptr, "ignored");  // Must not crash.
    prk_skip_test(nullptr, "ignored");
}

void test_name_selection_rules(PrkTestContext* context, void*) {
    PRK_EXPECT(prk_is_test_name_selected("core.runner.x", nullptr) == 1);
    PRK_EXPECT(prk_is_test_name_selected("core.runner.x", "") == 1);
    PRK_EXPECT(prk_is_test_name_selected("core.runner.x", "runner") == 1);
    PRK_EXPECT(prk_is_test_name_selected("core.runner.x", "cli") == 0);
    PRK_EXPECT(prk_is_test_name_selected(nullptr, "") == 0);
}

}  // namespace

PrkStatus register_runner_tests(PrkRegistry& registry) {
    const PrkTestCase test_cases[] = {
        {"core.runner.runs_in_order_and_counts_outcomes", &test_runs_in_order_and_counts_outcomes, nullptr},
        {"core.runner.filter_selects_matching_names", &test_filter_selects_matching_names, nullptr},
        {"core.runner.uses_injected_clock_for_elapsed_ticks", &test_uses_injected_clock_for_elapsed_ticks, nullptr},
        {"core.runner.first_failure_message_is_kept", &test_first_failure_message_is_kept, nullptr},
        {"core.runner.long_messages_are_truncated", &test_long_messages_are_truncated, nullptr},
        {"core.runner.passes_user_state_and_runs_silently", &test_passes_user_state_and_runs_silently, nullptr},
        {"core.runner.rejects_null_registry", &test_rejects_null_registry, nullptr},
        {"core.runner.name_selection_rules", &test_name_selection_rules, nullptr},
    };
    return register_test_cases(registry, test_cases);
}

}  // namespace prk::selftest
