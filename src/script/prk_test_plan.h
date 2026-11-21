/**
 * @file prk_test_plan.h
 * @brief Engine-neutral test plan for script engines (#12): suites, hooks, tags, timeouts, skips, and
 *        the per-test execution order. The Lua engine (#9) uses it today; the JavaScript engine (#11)
 *        plugs in through the same PrkScriptEngineCalls table.
 *
 * Execution order for one test (all inside the core runner's call to the test body):
 *   1. before_all hooks of every enclosing suite that has not started yet (outermost first);
 *      a before_all failure fails every test of that suite with "before_all failed: ...".
 *   2. before_each hooks, outermost first; a failure skips the body.
 *   3. the test body.
 *   4. after_each hooks, innermost first (always run).
 *   5. after_all hooks of every suite whose last selected test this was (innermost first).
 * Must not perform IO; all script calls go through the engine table.
 */
#ifndef PROOFRUNKIT_PRK_TEST_PLAN_H
#define PROOFRUNKIT_PRK_TEST_PLAN_H

#include <stddef.h>
#include <stdint.h>

#include "proofrunkit/prk_script.h"

/** @brief Engine-specific reference to a script function (a Lua registry ref, a JS value slot, ...). */
typedef int64_t PrkScriptFunction;

/** @brief Outcome of calling one script function. */
typedef enum PrkScriptCallResult {
    PRK_SCRIPT_CALL_OK = 0,     /**< Returned normally. */
    PRK_SCRIPT_CALL_FAILED = 1, /**< Raised an assertion or any other error; message filled in. */
    PRK_SCRIPT_CALL_SKIPPED = 2 /**< Called skip_test; message holds the reason. */
} PrkScriptCallResult;

/** @brief Callbacks the plan uses to drive one engine. */
typedef struct PrkScriptEngineCalls {
    /** Starts a test: resets the instruction budget and sets the wall-clock deadline (0 = none). */
    void (*begin_test)(void* engine, uint64_t timeout_ms);
    /** Calls @p function with no arguments; writes a NUL-terminated message unless the result is OK. */
    PrkScriptCallResult (*call_function)(void* engine, PrkScriptFunction function, char* out_message,
                                         size_t message_capacity);
    /** Releases @p function; called once per handle the plan accepted. */
    void (*release_function)(void* engine, PrkScriptFunction function);
    void* engine; /**< Passed back to every callback. Not owned. */
} PrkScriptEngineCalls;

/** @brief Kinds of suite hooks. */
typedef enum PrkSuiteHookKind {
    PRK_HOOK_BEFORE_ALL = 0,
    PRK_HOOK_AFTER_ALL = 1,
    PRK_HOOK_BEFORE_EACH = 2,
    PRK_HOOK_AFTER_EACH = 3,
    PRK_HOOK_KIND_COUNT = 4
} PrkSuiteHookKind;

/** @brief Options for one test, as written in the script. */
typedef struct PrkPlannedTestOptions {
    const char* name;        /**< Test name within its suite; non-empty. */
    PrkScriptFunction body;  /**< Test body; ownership passes to the plan. */
    const char* const* tags; /**< Tags on the test itself (suite tags are inherited). */
    size_t tag_count;        /**< Entries in @p tags. */
    uint64_t timeout_ms;     /**< Wall-clock timeout; 0 uses the run's default. */
    const char* skip_reason; /**< Non-NULL registers the test as skipped without running it. */
} PrkPlannedTestOptions;

/** @brief Opaque test plan for one script. */
typedef struct PrkTestPlan PrkTestPlan;

/**
 * @brief Creates an empty plan with only the root suite (the script itself).
 * @param engine_calls Engine callbacks (copied).
 * @param settings Selection and default timeout (copied, including tag strings); NULL selects all.
 * @return The plan, or NULL when memory runs out.
 */
PrkTestPlan* prk_create_test_plan(const PrkScriptEngineCalls* engine_calls, const PrkScriptRunSettings* settings);

/**
 * @brief Releases every function handle through the engine, then frees the plan. NULL does nothing.
 * @param plan Plan to destroy. Registries holding its tests must not be run afterwards.
 */
void prk_destroy_test_plan(PrkTestPlan* plan);

/**
 * @brief Opens a nested suite; tests and hooks added until prk_end_test_suite() belong to it.
 * @param plan Plan being built.
 * @param name Suite name; non-empty. Full test names are "suite.sub.test".
 * @param tags Tags inherited by every test in the suite.
 * @param tag_count Entries in @p tags.
 * @return PRK_STATUS_OK, PRK_STATUS_INVALID_ARGUMENT, or PRK_STATUS_OUT_OF_MEMORY.
 */
PrkStatus prk_begin_test_suite(PrkTestPlan* plan, const char* name, const char* const* tags, size_t tag_count);

/**
 * @brief Closes the innermost open suite.
 * @return PRK_STATUS_OK, or PRK_STATUS_INVALID_ARGUMENT when only the root suite is open.
 */
PrkStatus prk_end_test_suite(PrkTestPlan* plan);

/**
 * @brief Adds a hook to the innermost open suite. Ownership of @p function passes to the plan even on failure.
 * @return PRK_STATUS_OK, PRK_STATUS_INVALID_ARGUMENT, or PRK_STATUS_OUT_OF_MEMORY.
 */
PrkStatus prk_add_suite_hook(PrkTestPlan* plan, PrkSuiteHookKind kind, PrkScriptFunction function);

/**
 * @brief Adds a test to the innermost open suite and, when selected, registers it in @p registry.
 *
 * Ownership of options->body passes to the plan even on failure (unselected bodies are released at once).
 *
 * @param plan Plan being built.
 * @param registry Registry that receives the test under its full name.
 * @param options Test description.
 * @param out_was_selected Receives 1 when registered, 0 when filtered out; may be NULL.
 * @return PRK_STATUS_OK, PRK_STATUS_INVALID_ARGUMENT, PRK_STATUS_DUPLICATE_NAME, or PRK_STATUS_OUT_OF_MEMORY.
 */
PrkStatus prk_add_planned_test(PrkTestPlan* plan, PrkRegistry* registry, const PrkPlannedTestOptions* options,
                               int* out_was_selected);

/**
 * @brief Counts the tests the plan registered (selected tests only).
 * @param plan Plan to inspect; NULL counts as zero.
 * @return Number of registered tests.
 */
size_t prk_count_planned_tests(const PrkTestPlan* plan);

/**
 * @brief Returns the depth of open suites (0 = only the root). Engines use it to reject unbalanced suites.
 * @param plan Plan to inspect; NULL returns 0.
 * @return Number of open nested suites.
 */
size_t prk_count_open_test_suites(const PrkTestPlan* plan);

#endif /* PROOFRUNKIT_PRK_TEST_PLAN_H */
