/**
 * @file prk_test_plan.c
 * @brief Implementation of the engine-neutral test plan (#12). See prk_test_plan.h for the execution order.
 *
 * Size: over the 400-line review mark; building the plan and executing it share private types, and splitting
 * them would expose those types in a header used by both engines.
 */
#include "prk_test_plan.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/** @brief Deepest allowed suite nesting; keeps the per-test suite chain on the stack. */
#define PRK_MAX_SUITE_DEPTH 32

/** @brief States of a suite's before_all hooks. */
enum { PRK_SUITE_NOT_STARTED = 0, PRK_SUITE_STARTED = 1, PRK_SUITE_SETUP_FAILED = 2 };

typedef struct PrkFunctionList {
    PrkScriptFunction* items;
    size_t count;
    size_t capacity;
} PrkFunctionList;

typedef struct PrkTagList {
    char** items;
    size_t count;
} PrkTagList;

typedef struct PrkPlannedSuite {
    char* full_name;                            /**< "" for the root suite. */
    size_t parent;                              /**< Parent index; the root is its own parent. */
    size_t depth;                               /**< 0 for the root. */
    PrkTagList tags;                            /**< Tags inherited by the suite's tests. */
    PrkFunctionList hooks[PRK_HOOK_KIND_COUNT]; /**< Hooks by kind, in registration order. */
    size_t remaining_tests;                     /**< Selected tests (here or nested) not finished yet. */
    int setup_state;                            /**< PRK_SUITE_NOT_STARTED / STARTED / SETUP_FAILED. */
    char* setup_error;                          /**< "before_all failed: ..." when setup failed. */
} PrkPlannedSuite;

typedef struct PrkPlannedTest {
    PrkTestPlan* plan;
    size_t suite;
    PrkScriptFunction body;
    uint64_t timeout_ms;
    char* skip_reason;
} PrkPlannedTest;

struct PrkTestPlan {
    PrkScriptEngineCalls engine;
    char* name_filter;
    PrkTagList include_tags;
    PrkTagList exclude_tags;
    uint64_t default_timeout_ms;
    PrkPlannedSuite* suites;
    size_t suite_count;
    size_t suite_capacity;
    size_t current_suite;
    PrkPlannedTest** tests;
    size_t test_count;
    size_t test_capacity;
};

/** @brief Returns a malloc'd copy of @p text, or NULL. */
static char* prk_copy_plan_text(const char* text) {
    const size_t length = strlen(text);
    char* const copy = (char*)malloc(length + 1);
    if (copy != NULL) {
        memcpy(copy, text, length + 1);
    }
    return copy;
}

static void prk_free_tag_list(PrkTagList* list) {
    for (size_t index = 0; index < list->count; ++index) {
        free(list->items[index]);
    }
    free(list->items);
    list->items = NULL;
    list->count = 0;
}

/** @brief Copies @p count tags into @p out_list. @return 1 on success, 0 when memory runs out. */
static int prk_copy_tag_list(const char* const* tags, size_t count, PrkTagList* out_list) {
    out_list->items = NULL;
    out_list->count = 0;
    if (count == 0) {
        return 1;
    }
    out_list->items = (char**)calloc(count, sizeof(char*));
    if (out_list->items == NULL) {
        return 0;
    }
    for (size_t index = 0; index < count; ++index) {
        out_list->items[index] = prk_copy_plan_text(tags[index] != NULL ? tags[index] : "");
        if (out_list->items[index] == NULL) {
            out_list->count = index;
            prk_free_tag_list(out_list);
            return 0;
        }
    }
    out_list->count = count;
    return 1;
}

static int prk_is_tag_in_list(const PrkTagList* list, const char* tag) {
    for (size_t index = 0; index < list->count; ++index) {
        if (strcmp(list->items[index], tag) == 0) {
            return 1;
        }
    }
    return 0;
}

/** @brief Appends @p function to @p list. @return 1 on success, 0 when memory runs out. */
static int prk_append_function(PrkFunctionList* list, PrkScriptFunction function) {
    if (list->count == list->capacity) {
        const size_t capacity = list->capacity == 0 ? 4 : list->capacity * 2;
        PrkScriptFunction* const grown = (PrkScriptFunction*)realloc(list->items, capacity * sizeof(PrkScriptFunction));
        if (grown == NULL) {
            return 0;
        }
        list->items = grown;
        list->capacity = capacity;
    }
    list->items[list->count] = function;
    list->count += 1;
    return 1;
}

/** @brief Appends a suite. @return 1 on success, 0 when memory runs out (nothing is taken over). */
static int prk_append_suite(PrkTestPlan* plan, const PrkPlannedSuite* suite) {
    if (plan->suite_count == plan->suite_capacity) {
        const size_t capacity = plan->suite_capacity == 0 ? 4 : plan->suite_capacity * 2;
        PrkPlannedSuite* const grown = (PrkPlannedSuite*)realloc(plan->suites, capacity * sizeof(PrkPlannedSuite));
        if (grown == NULL) {
            return 0;
        }
        plan->suites = grown;
        plan->suite_capacity = capacity;
    }
    plan->suites[plan->suite_count] = *suite;
    plan->suite_count += 1;
    return 1;
}

PrkTestPlan* prk_create_test_plan(const PrkScriptEngineCalls* engine_calls, const PrkScriptRunSettings* settings) {
    if (engine_calls == NULL) {
        return NULL;
    }
    PrkTestPlan* const plan = (PrkTestPlan*)calloc(1, sizeof(PrkTestPlan));
    if (plan == NULL) {
        return NULL;
    }
    plan->engine = *engine_calls;
    const PrkScriptRunSettings no_settings = {NULL, NULL, 0, NULL, 0, 0};
    const PrkScriptRunSettings* const chosen = settings != NULL ? settings : &no_settings;
    plan->default_timeout_ms = chosen->default_timeout_ms;
    plan->name_filter = prk_copy_plan_text(chosen->name_filter != NULL ? chosen->name_filter : "");
    PrkPlannedSuite root;
    memset(&root, 0, sizeof root);
    root.full_name = prk_copy_plan_text("");
    const int is_ready = plan->name_filter != NULL && root.full_name != NULL &&
                         prk_copy_tag_list(chosen->include_tags, chosen->include_tag_count, &plan->include_tags) &&
                         prk_copy_tag_list(chosen->exclude_tags, chosen->exclude_tag_count, &plan->exclude_tags) &&
                         prk_append_suite(plan, &root);
    if (!is_ready) {
        free(root.full_name);
        prk_destroy_test_plan(plan);
        return NULL;
    }
    return plan;
}

void prk_destroy_test_plan(PrkTestPlan* plan) {
    if (plan == NULL) {
        return;
    }
    for (size_t index = 0; index < plan->test_count; ++index) {
        PrkPlannedTest* const test = plan->tests[index];
        plan->engine.release_function(plan->engine.engine, test->body);
        free(test->skip_reason);
        free(test);
    }
    for (size_t index = 0; index < plan->suite_count; ++index) {
        PrkPlannedSuite* const suite = &plan->suites[index];
        for (int kind = 0; kind < PRK_HOOK_KIND_COUNT; ++kind) {
            for (size_t hook = 0; hook < suite->hooks[kind].count; ++hook) {
                plan->engine.release_function(plan->engine.engine, suite->hooks[kind].items[hook]);
            }
            free(suite->hooks[kind].items);
        }
        prk_free_tag_list(&suite->tags);
        free(suite->full_name);
        free(suite->setup_error);
    }
    free(plan->suites);
    free(plan->tests);
    prk_free_tag_list(&plan->include_tags);
    prk_free_tag_list(&plan->exclude_tags);
    free(plan->name_filter);
    free(plan);
}

/** @brief Returns "<prefix>.<name>" (or just name for the root), malloc'd, or NULL. */
static char* prk_join_test_name(const char* prefix, const char* name) {
    const size_t prefix_length = strlen(prefix);
    const size_t name_length = strlen(name);
    char* const joined = (char*)malloc(prefix_length + name_length + 2);
    if (joined == NULL) {
        return NULL;
    }
    if (prefix_length == 0) {
        memcpy(joined, name, name_length + 1);
    } else {
        memcpy(joined, prefix, prefix_length);
        joined[prefix_length] = '.';
        memcpy(joined + prefix_length + 1, name, name_length + 1);
    }
    return joined;
}

PrkStatus prk_begin_test_suite(PrkTestPlan* plan, const char* name, const char* const* tags, size_t tag_count) {
    if (plan == NULL || name == NULL || name[0] == '\0' ||
        plan->suites[plan->current_suite].depth + 1 >= PRK_MAX_SUITE_DEPTH) {
        return PRK_STATUS_INVALID_ARGUMENT;
    }
    PrkPlannedSuite suite;
    memset(&suite, 0, sizeof suite);
    suite.parent = plan->current_suite;
    suite.depth = plan->suites[plan->current_suite].depth + 1;
    suite.full_name = prk_join_test_name(plan->suites[plan->current_suite].full_name, name);
    if (suite.full_name == NULL || !prk_copy_tag_list(tags, tag_count, &suite.tags) ||
        !prk_append_suite(plan, &suite)) {
        free(suite.full_name);
        prk_free_tag_list(&suite.tags);
        return PRK_STATUS_OUT_OF_MEMORY;
    }
    plan->current_suite = plan->suite_count - 1;
    return PRK_STATUS_OK;
}

PrkStatus prk_end_test_suite(PrkTestPlan* plan) {
    if (plan == NULL || plan->current_suite == 0) {
        return PRK_STATUS_INVALID_ARGUMENT;
    }
    plan->current_suite = plan->suites[plan->current_suite].parent;
    return PRK_STATUS_OK;
}

size_t prk_count_open_test_suites(const PrkTestPlan* plan) {
    return plan == NULL ? 0 : plan->suites[plan->current_suite].depth;
}

PrkStatus prk_add_suite_hook(PrkTestPlan* plan, PrkSuiteHookKind kind, PrkScriptFunction function) {
    if (plan == NULL) {
        return PRK_STATUS_INVALID_ARGUMENT;
    }
    if ((int)kind < 0 || kind >= PRK_HOOK_KIND_COUNT) {
        plan->engine.release_function(plan->engine.engine, function);
        return PRK_STATUS_INVALID_ARGUMENT;
    }
    if (!prk_append_function(&plan->suites[plan->current_suite].hooks[kind], function)) {
        plan->engine.release_function(plan->engine.engine, function);
        return PRK_STATUS_OUT_OF_MEMORY;
    }
    return PRK_STATUS_OK;
}

/** @brief Reports whether the test (own tags plus suite tags) carries @p tag. */
static int prk_has_planned_tag(const PrkTestPlan* plan, size_t suite, const PrkPlannedTestOptions* options,
                               const char* tag) {
    for (size_t index = 0; index < options->tag_count; ++index) {
        if (options->tags[index] != NULL && strcmp(options->tags[index], tag) == 0) {
            return 1;
        }
    }
    for (size_t current = suite;; current = plan->suites[current].parent) {
        if (prk_is_tag_in_list(&plan->suites[current].tags, tag)) {
            return 1;
        }
        if (current == 0) {
            return 0;
        }
    }
}

/** @brief Applies the include/exclude tag rules. */
static int prk_is_tag_selection_met(const PrkTestPlan* plan, size_t suite, const PrkPlannedTestOptions* options) {
    for (size_t index = 0; index < plan->exclude_tags.count; ++index) {
        if (prk_has_planned_tag(plan, suite, options, plan->exclude_tags.items[index])) {
            return 0;
        }
    }
    if (plan->include_tags.count == 0) {
        return 1;
    }
    for (size_t index = 0; index < plan->include_tags.count; ++index) {
        if (prk_has_planned_tag(plan, suite, options, plan->include_tags.items[index])) {
            return 1;
        }
    }
    return 0;
}

static void prk_run_planned_test(PrkTestContext* context, void* test_state);

/** @brief Stores @p test in the plan's list. @return 1 on success, 0 when memory runs out. */
static int prk_append_planned_test(PrkTestPlan* plan, PrkPlannedTest* test) {
    if (plan->test_count == plan->test_capacity) {
        const size_t capacity = plan->test_capacity == 0 ? 8 : plan->test_capacity * 2;
        PrkPlannedTest** const grown = (PrkPlannedTest**)realloc(plan->tests, capacity * sizeof(PrkPlannedTest*));
        if (grown == NULL) {
            return 0;
        }
        plan->tests = grown;
        plan->test_capacity = capacity;
    }
    plan->tests[plan->test_count] = test;
    plan->test_count += 1;
    return 1;
}

/** @brief Counts the new test in every enclosing suite so after_all runs after the last one. */
static void prk_count_test_in_suites(PrkTestPlan* plan, size_t suite) {
    for (size_t current = suite;; current = plan->suites[current].parent) {
        plan->suites[current].remaining_tests += 1;
        if (current == 0) {
            return;
        }
    }
}

PrkStatus prk_add_planned_test(PrkTestPlan* plan, PrkRegistry* registry, const PrkPlannedTestOptions* options,
                               int* out_was_selected) {
    if (out_was_selected != NULL) {
        *out_was_selected = 0;
    }
    if (plan == NULL || options == NULL) {
        return PRK_STATUS_INVALID_ARGUMENT;
    }
    if (registry == NULL || options->name == NULL || options->name[0] == '\0') {
        plan->engine.release_function(plan->engine.engine, options->body);
        return PRK_STATUS_INVALID_ARGUMENT;
    }
    const size_t suite = plan->current_suite;
    char* const full_name = prk_join_test_name(plan->suites[suite].full_name, options->name);
    if (full_name == NULL) {
        plan->engine.release_function(plan->engine.engine, options->body);
        return PRK_STATUS_OUT_OF_MEMORY;
    }
    if (!prk_is_test_name_selected(full_name, plan->name_filter) || !prk_is_tag_selection_met(plan, suite, options)) {
        free(full_name);
        plan->engine.release_function(plan->engine.engine, options->body);
        return PRK_STATUS_OK;
    }
    PrkPlannedTest* const test = (PrkPlannedTest*)calloc(1, sizeof(PrkPlannedTest));
    PrkStatus status = PRK_STATUS_OUT_OF_MEMORY;
    if (test != NULL) {
        test->plan = plan;
        test->suite = suite;
        test->body = options->body;
        test->timeout_ms = options->timeout_ms != 0 ? options->timeout_ms : plan->default_timeout_ms;
        test->skip_reason = options->skip_reason != NULL ? prk_copy_plan_text(options->skip_reason) : NULL;
        const int is_complete = options->skip_reason == NULL || test->skip_reason != NULL;
        const PrkTestCase test_case = {full_name, &prk_run_planned_test, test};
        status = is_complete ? prk_register_test_case(registry, &test_case) : PRK_STATUS_OUT_OF_MEMORY;
        if (status == PRK_STATUS_OK && !prk_append_planned_test(plan, test)) {
            status = PRK_STATUS_OUT_OF_MEMORY; /* The registry keeps a test we cannot run safely; fail the load. */
        }
    }
    free(full_name);
    if (status != PRK_STATUS_OK) {
        plan->engine.release_function(plan->engine.engine, options->body);
        if (test != NULL) {
            free(test->skip_reason);
        }
        free(test);
        return status;
    }
    prk_count_test_in_suites(plan, suite);
    if (out_was_selected != NULL) {
        *out_was_selected = 1;
    }
    return PRK_STATUS_OK;
}

size_t prk_count_planned_tests(const PrkTestPlan* plan) {
    return plan == NULL ? 0 : plan->test_count;
}

/* ------------------------------------------------------------------ execution */

/** @brief Fails @p context with "<prefix><message>". */
static void prk_fail_with_prefix(PrkTestContext* context, const char* prefix, const char* message) {
    char combined[PRK_MESSAGE_CAPACITY + 64];
    (void)snprintf(combined, sizeof combined, "%s%s", prefix, message);
    prk_fail_test(context, combined);
}

/**
 * @brief Calls every function in @p list (in order, or reversed) until one does not return OK.
 * @return The first non-OK result, with its message in @p out_message; OK when all returned.
 */
static PrkScriptCallResult prk_call_function_list(const PrkTestPlan* plan, const PrkFunctionList* list, int is_reversed,
                                                  char* out_message, size_t capacity) {
    for (size_t offset = 0; offset < list->count; ++offset) {
        const size_t index = is_reversed ? list->count - 1 - offset : offset;
        const PrkScriptCallResult result =
            plan->engine.call_function(plan->engine.engine, list->items[index], out_message, capacity);
        if (result != PRK_SCRIPT_CALL_OK) {
            return result;
        }
    }
    return PRK_SCRIPT_CALL_OK;
}

/** @brief Runs pending before_all hooks along @p chain. @return 1 when every suite is set up. */
static int prk_start_suites(PrkTestPlan* plan, const size_t* chain, size_t chain_length, PrkTestContext* context) {
    char message[PRK_MESSAGE_CAPACITY];
    for (size_t index = 0; index < chain_length; ++index) {
        PrkPlannedSuite* const suite = &plan->suites[chain[index]];
        if (suite->setup_state == PRK_SUITE_NOT_STARTED) {
            const PrkFunctionList* const hooks = &suite->hooks[PRK_HOOK_BEFORE_ALL];
            const PrkScriptCallResult result = prk_call_function_list(plan, hooks, 0, message, sizeof message);
            suite->setup_state = result == PRK_SCRIPT_CALL_OK ? PRK_SUITE_STARTED : PRK_SUITE_SETUP_FAILED;
            if (result != PRK_SCRIPT_CALL_OK) {
                char combined[PRK_MESSAGE_CAPACITY + 64];
                (void)snprintf(combined, sizeof combined, "before_all failed: %s", message);
                suite->setup_error = prk_copy_plan_text(combined);
            }
        }
        if (suite->setup_state == PRK_SUITE_SETUP_FAILED) {
            prk_fail_test(context, suite->setup_error != NULL ? suite->setup_error : "before_all failed");
            return 0;
        }
    }
    return 1;
}

/** @brief Runs before_each hooks, the body, and after_each hooks for a started test. */
static void prk_run_test_with_each_hooks(const PrkPlannedTest* test, const size_t* chain, size_t chain_length,
                                         PrkTestContext* context) {
    const PrkTestPlan* const plan = test->plan;
    char message[PRK_MESSAGE_CAPACITY];
    PrkScriptCallResult result = PRK_SCRIPT_CALL_OK;
    for (size_t index = 0; index < chain_length && result == PRK_SCRIPT_CALL_OK; ++index) {
        result = prk_call_function_list(plan, &plan->suites[chain[index]].hooks[PRK_HOOK_BEFORE_EACH], 0, message,
                                        sizeof message);
        if (result == PRK_SCRIPT_CALL_FAILED) {
            prk_fail_with_prefix(context, "before_each failed: ", message);
        }
    }
    if (result == PRK_SCRIPT_CALL_OK) {
        result = plan->engine.call_function(plan->engine.engine, test->body, message, sizeof message);
        if (result == PRK_SCRIPT_CALL_FAILED) {
            prk_fail_test(context, message);
        }
    }
    if (result == PRK_SCRIPT_CALL_SKIPPED) {
        prk_skip_test(context, message);
    }
    for (size_t offset = 0; offset < chain_length; ++offset) {
        const size_t index = chain[chain_length - 1 - offset];
        if (prk_call_function_list(plan, &plan->suites[index].hooks[PRK_HOOK_AFTER_EACH], 1, message, sizeof message) ==
            PRK_SCRIPT_CALL_FAILED) {
            prk_fail_with_prefix(context, "after_each failed: ", message);
        }
    }
}

/** @brief Counts the test as finished in every enclosing suite and runs after_all where it was the last. */
static void prk_finish_suites(PrkTestPlan* plan, const size_t* chain, size_t chain_length, PrkTestContext* context) {
    char message[PRK_MESSAGE_CAPACITY];
    for (size_t offset = 0; offset < chain_length; ++offset) {
        PrkPlannedSuite* const suite = &plan->suites[chain[chain_length - 1 - offset]];
        suite->remaining_tests -= 1;
        if (suite->remaining_tests == 0 && suite->setup_state == PRK_SUITE_STARTED &&
            prk_call_function_list(plan, &suite->hooks[PRK_HOOK_AFTER_ALL], 0, message, sizeof message) ==
                PRK_SCRIPT_CALL_FAILED) {
            prk_fail_with_prefix(context, "after_all failed: ", message);
        }
    }
}

/** @brief PrkTestBody for every planned test. */
static void prk_run_planned_test(PrkTestContext* context, void* test_state) {
    const PrkPlannedTest* const test = (const PrkPlannedTest*)test_state;
    PrkTestPlan* const plan = test->plan;
    size_t chain[PRK_MAX_SUITE_DEPTH];
    const size_t chain_length = plan->suites[test->suite].depth + 1;
    for (size_t current = test->suite, index = chain_length; index > 0; current = plan->suites[current].parent) {
        chain[--index] = current;
    }
    plan->engine.begin_test(plan->engine.engine, test->timeout_ms);
    if (test->skip_reason != NULL) {
        prk_skip_test(context, test->skip_reason);
    } else if (prk_start_suites(plan, chain, chain_length, context)) {
        prk_run_test_with_each_hooks(test, chain, chain_length, context);
    }
    prk_finish_suites(plan, chain, chain_length, context);
}
