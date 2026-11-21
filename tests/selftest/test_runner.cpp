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

}  // namespace

PrkStatus register_runner_tests(PrkRegistry& registry) {
    const PrkTestCase test_cases[] = {
        {"core.runner.uses_injected_clock_for_elapsed_ticks", &test_uses_injected_clock_for_elapsed_ticks, nullptr},
        {"core.runner.long_messages_are_truncated", &test_long_messages_are_truncated, nullptr},
        {"core.runner.passes_user_state_and_runs_silently", &test_passes_user_state_and_runs_silently, nullptr},
        {"core.runner.rejects_null_registry", &test_rejects_null_registry, nullptr},
    };
    return register_test_cases(registry, test_cases);
}

}  // namespace prk::selftest
