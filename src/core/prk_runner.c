/**
 * @file prk_runner.c
 * @brief Runs registered test cases in order, timing each with an injected clock and reporting results.
 */
#include <string.h>

#include "prk_internal.h"

/** @brief Copies @p text into the context message buffer, truncating to the context capacity. */
static void store_context_message(PrkTestContext* context, const char* text) {
    const char* const source = text == NULL ? "" : text;
    size_t length = strlen(source);
    if (length >= PRK_MESSAGE_CAPACITY) {
        length = PRK_MESSAGE_CAPACITY - 1;
    }
    memcpy(context->message, source, length);
    context->message[length] = '\0';
}

void prk_fail_test(PrkTestContext* context, const char* message) {
    if (context == NULL || context->outcome == PRK_OUTCOME_FAILED) {
        return;
    }
    context->outcome = PRK_OUTCOME_FAILED;
    store_context_message(context, message);
}

void prk_skip_test(PrkTestContext* context, const char* reason) {
    if (context == NULL || context->outcome != PRK_OUTCOME_PASSED) {
        return;
    }
    context->outcome = PRK_OUTCOME_SKIPPED;
    store_context_message(context, reason);
}

int prk_is_test_name_selected(const char* test_name, const char* name_filter) {
    if (test_name == NULL) {
        return 0;
    }
    if (name_filter == NULL || name_filter[0] == '\0') {
        return 1;
    }
    return strstr(test_name, name_filter) != NULL;
}

/** @brief Reads the injected clock, or returns 0 when no clock was supplied. */
static uint64_t read_injected_clock(const PrkClock* clock) {
    return clock->read_ticks == NULL ? 0 : clock->read_ticks(clock->clock_state);
}

/** @brief Counts how many registered names the filter selects. */
static size_t count_selected_test_cases(const PrkRegistry* registry, const char* name_filter) {
    size_t selected = 0;
    for (size_t index = 0; index < registry->count; ++index) {
        selected += (size_t)prk_is_test_name_selected(registry->entries[index].name, name_filter);
    }
    return selected;
}

/** @brief Runs one test body and returns its outcome; the result message points into @p context. */
static PrkTestResult run_one_test_case(const PrkRegisteredTestCase* entry, const PrkClock* clock,
                                       PrkTestContext* context) {
    context->outcome = PRK_OUTCOME_PASSED;
    context->message[0] = '\0';
    const uint64_t started_ticks = read_injected_clock(clock);
    entry->body(context, entry->user_state);
    const uint64_t finished_ticks = read_injected_clock(clock);
    const PrkTestResult result = {entry->name, context->outcome, context->message,
                                  finished_ticks >= started_ticks ? finished_ticks - started_ticks : 0};
    return result;
}

PrkStatus prk_run_test_cases(const PrkRegistry* registry, const PrkRunOptions* options, const PrkReporter* reporter,
                             PrkRunSummary* out_summary) {
    if (registry == NULL) {
        return PRK_STATUS_INVALID_ARGUMENT;
    }
    const PrkRunOptions default_options = {NULL, {NULL, NULL}};
    const PrkRunOptions* const effective_options = options == NULL ? &default_options : options;
    const PrkReporter silent_reporter = {NULL, NULL, NULL, NULL};
    const PrkReporter* const effective_reporter = reporter == NULL ? &silent_reporter : reporter;

    if (effective_reporter->report_run_started != NULL) {
        effective_reporter->report_run_started(effective_reporter->reporter_state,
                                               count_selected_test_cases(registry, effective_options->name_filter));
    }
    PrkRunSummary summary = {0, 0, 0, 0};
    PrkTestContext context;
    for (size_t index = 0; index < registry->count; ++index) {
        const PrkRegisteredTestCase* const entry = &registry->entries[index];
        if (!prk_is_test_name_selected(entry->name, effective_options->name_filter)) {
            continue;
        }
        const PrkTestResult result = run_one_test_case(entry, &effective_options->clock, &context);
        summary = prk_add_outcome_to_summary(summary, result.outcome);
        if (effective_reporter->report_test_finished != NULL) {
            effective_reporter->report_test_finished(effective_reporter->reporter_state, &result);
        }
    }
    if (effective_reporter->report_run_finished != NULL) {
        effective_reporter->report_run_finished(effective_reporter->reporter_state, &summary);
    }
    if (out_summary != NULL) {
        *out_summary = summary;
    }
    return PRK_STATUS_OK;
}
