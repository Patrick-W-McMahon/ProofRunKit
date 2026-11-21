/**
 * @file prk.h
 * @brief Public C ABI of the ProofRunKit core: register test cases, run them, and receive results
 *        through reporter callbacks.
 *
 * Everything in this header is plain C17 so that the C++ front end, the future Lua and JavaScript
 * engines (#9, #11), and the compiler drivers (#13-#16) can all talk to the same core. Tests are
 * registered at runtime; they are never compiled into the kit binary (#22).
 */
#ifndef PROOFRUNKIT_PRK_H
#define PROOFRUNKIT_PRK_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/** @brief Status codes returned by core functions that can fail. */
typedef enum PrkStatus {
    PRK_STATUS_OK = 0,               /**< The call succeeded. */
    PRK_STATUS_INVALID_ARGUMENT = 1, /**< A required pointer was NULL or a name was empty. */
    PRK_STATUS_OUT_OF_MEMORY = 2,    /**< An allocation failed; the registry is unchanged. */
    PRK_STATUS_DUPLICATE_NAME = 3    /**< A test case with the same name is already registered. */
} PrkStatus;

/** @brief Final outcome of one test case. */
typedef enum PrkOutcome {
    PRK_OUTCOME_PASSED = 0, /**< The body returned without failing or skipping. */
    PRK_OUTCOME_FAILED = 1, /**< The body called prk_fail_test(). */
    PRK_OUTCOME_SKIPPED = 2 /**< The body called prk_skip_test() and did not fail. */
} PrkOutcome;

/** @brief Maximum stored length (including the terminator) of a failure or skip message. */
#define PRK_MESSAGE_CAPACITY 512

/**
 * @brief Per-test context handed to a test body. Opaque to callers; the runner owns it.
 *
 * A body reports a failure or a skip through prk_fail_test() / prk_skip_test(). The context lives
 * only for the duration of one test body call.
 */
typedef struct PrkTestContext PrkTestContext;

/**
 * @brief Signature of a test body.
 * @param context Context for this run of the test; use it to fail or skip.
 * @param user_state The pointer given at registration time; may be NULL.
 */
typedef void (*PrkTestBody)(PrkTestContext* context, void* user_state);

/** @brief Description of a test case to register. The registry copies @p name. */
typedef struct PrkTestCase {
    const char* name; /**< Unique, non-empty name, e.g. "core.registry.rejects_duplicates". */
    PrkTestBody body; /**< Function that runs the test; must not be NULL. */
    void* user_state; /**< Passed back to @p body unchanged; may be NULL. Not owned. */
} PrkTestCase;

/** @brief Opaque, growable list of registered test cases. */
typedef struct PrkRegistry PrkRegistry;

/** @brief Result of one test case as delivered to a reporter. Valid only during the callback. */
typedef struct PrkTestResult {
    const char* name;       /**< Registered test name. */
    PrkOutcome outcome;     /**< Final outcome. */
    const char* message;    /**< Failure or skip message; empty string when the test passed. */
    uint64_t elapsed_ticks; /**< Clock ticks spent in the body (unit depends on the injected clock). */
} PrkTestResult;

/** @brief Totals for one run. */
typedef struct PrkRunSummary {
    size_t total;   /**< Test cases that were selected and executed. */
    size_t passed;  /**< Test cases that passed. */
    size_t failed;  /**< Test cases that failed. */
    size_t skipped; /**< Test cases that were skipped. */
} PrkRunSummary;

/**
 * @brief Callbacks that receive progress and results. Any callback may be NULL.
 *
 * The core never prints; all output goes through a reporter supplied by the caller.
 */
typedef struct PrkReporter {
    /** Called once before the first test with the number of selected tests. */
    void (*report_run_started)(void* reporter_state, size_t selected_test_count);
    /** Called after each test case finishes. */
    void (*report_test_finished)(void* reporter_state, const PrkTestResult* result);
    /** Called once after the last test with the final totals. */
    void (*report_run_finished)(void* reporter_state, const PrkRunSummary* summary);
    /** Passed back to every callback unchanged; may be NULL. Not owned. */
    void* reporter_state;
} PrkReporter;

/**
 * @brief Clock used to time test bodies. Injected so tests can use a fake clock.
 *
 * When @p read_ticks is NULL the runner records zero elapsed ticks.
 */
typedef struct PrkClock {
    uint64_t (*read_ticks)(void* clock_state); /**< Returns the current tick count. */
    void* clock_state;                         /**< Passed back to @p read_ticks; may be NULL. */
} PrkClock;

/** @brief Options for prk_run_test_cases(). Zero-initialise for defaults. */
typedef struct PrkRunOptions {
    const char* name_filter; /**< Run only tests whose name contains this text; NULL or "" runs all. */
    PrkClock clock;          /**< Clock used for elapsed_ticks; a zeroed clock disables timing. */
} PrkRunOptions;

/* ---------------------------------------------------------------- registry */

/**
 * @brief Allocates an empty registry.
 * @return The new registry, or NULL if memory could not be allocated. Free with prk_destroy_registry().
 */
PrkRegistry* prk_create_registry(void);

/**
 * @brief Frees a registry and the names it copied. Passing NULL does nothing.
 * @param registry Registry created by prk_create_registry().
 */
void prk_destroy_registry(PrkRegistry* registry);

/**
 * @brief Adds a test case to the end of the registry.
 * @param registry Registry to add to.
 * @param test_case Test case to copy in; the name is duplicated, user_state is not owned.
 * @return PRK_STATUS_OK, PRK_STATUS_INVALID_ARGUMENT (NULL pointer, empty name, NULL body),
 *         PRK_STATUS_DUPLICATE_NAME, or PRK_STATUS_OUT_OF_MEMORY. On failure the registry is unchanged.
 */
PrkStatus prk_register_test_case(PrkRegistry* registry, const PrkTestCase* test_case);

/**
 * @brief Counts the registered test cases.
 * @param registry Registry to inspect; NULL counts as empty.
 * @return Number of registered test cases.
 */
size_t prk_count_test_cases(const PrkRegistry* registry);

/**
 * @brief Returns the name of the test case at @p index (registration order).
 * @param registry Registry to inspect.
 * @param index Zero-based index.
 * @return The stored name, or NULL if @p registry is NULL or @p index is out of range. The pointer
 *         stays valid until the registry is destroyed.
 */
const char* prk_get_test_case_name(const PrkRegistry* registry, size_t index);

/* ------------------------------------------------------------------ runner */

/**
 * @brief Reports whether a test name is selected by a name filter.
 * @param test_name Name to check; NULL is never selected.
 * @param name_filter Substring to look for; NULL or "" selects every name.
 * @return 1 if selected, 0 otherwise. Pure function.
 */
int prk_is_test_name_selected(const char* test_name, const char* name_filter);

/**
 * @brief Runs every selected test case in registration order and reports each result.
 *
 * Test bodies run in-process on the calling thread. Crash isolation is future work for the drivers.
 *
 * @param registry Test cases to run.
 * @param options Filter and clock; NULL means run everything without timing.
 * @param reporter Callbacks for progress; NULL runs silently.
 * @param out_summary Receives the totals; may be NULL.
 * @return PRK_STATUS_OK, or PRK_STATUS_INVALID_ARGUMENT if @p registry is NULL.
 */
PrkStatus prk_run_test_cases(const PrkRegistry* registry, const PrkRunOptions* options, const PrkReporter* reporter,
                             PrkRunSummary* out_summary);

/**
 * @brief Marks the running test as failed. The first failure message is kept; later ones are ignored.
 * @param context Context passed to the test body.
 * @param message Human-readable reason; NULL is stored as an empty string. Truncated to
 *        PRK_MESSAGE_CAPACITY - 1 bytes.
 */
void prk_fail_test(PrkTestContext* context, const char* message);

/**
 * @brief Marks the running test as skipped unless it has already failed.
 * @param context Context passed to the test body.
 * @param reason Human-readable reason; NULL is stored as an empty string.
 */
void prk_skip_test(PrkTestContext* context, const char* reason);

/* --------------------------------------------------------------- reporting */

/**
 * @brief Returns a summary with one more result of the given outcome counted. Pure function.
 * @param summary Totals so far (passed by value; not modified).
 * @param outcome Outcome to add.
 * @return The updated totals.
 */
PrkRunSummary prk_add_outcome_to_summary(PrkRunSummary summary, PrkOutcome outcome);

/**
 * @brief Returns the lower-case name of an outcome ("passed", "failed", "skipped", or "unknown").
 * @param outcome Outcome to name.
 * @return A static string.
 */
const char* prk_get_outcome_name(PrkOutcome outcome);

/**
 * @brief Returns the human-readable name of a status code, e.g. "duplicate name".
 * @param status Status to name.
 * @return A static string.
 */
const char* prk_get_status_name(PrkStatus status);

/* ------------------------------------------------------------- hot paths */

/**
 * @brief Reads the CPU cycle counter (a hot path, implemented in Assembly where available).
 *
 * x86-64: RDTSC (result in RAX). 32-bit x86 (MSVC cdecl): RDTSC (result in EDX:EAX).
 * arm64: CNTVCT_EL0. When Assembly is unavailable or PRK_FORCE_C_FALLBACK is set, this forwards
 * to prk_read_cycle_counter_fallback(). The self-test in tests/selftest/test_cycle_counter.cpp
 * compares the two. The value is a pure read of a free-running counter: non-zero and
 * non-decreasing on one thread, with a platform-defined tick rate.
 *
 * @return Current counter value.
 */
uint64_t prk_read_cycle_counter(void);

/**
 * @brief Portable C fallback for prk_read_cycle_counter().
 *
 * Windows: QueryPerformanceCounter. Elsewhere: clock_gettime(CLOCK_MONOTONIC) in nanoseconds.
 *
 * @return Current counter value; non-zero and non-decreasing on one thread.
 */
uint64_t prk_read_cycle_counter_fallback(void);

/**
 * @brief Names the cycle-counter implementation selected at build time.
 * @return A static string such as "asm-masm-x64", "asm-masm-x86", "asm-gas-x86_64-sysv",
 *         "asm-gas-arm64", or "c-fallback".
 */
const char* prk_get_cycle_counter_implementation_name(void);

/**
 * @brief Reports whether prk_read_cycle_counter() is the Assembly implementation.
 * @return 1 when Assembly was built in, 0 when the C fallback is used.
 */
int prk_is_cycle_counter_assembly(void);

/* ---------------------------------------------------------------- version */

/**
 * @brief Returns the ProofRunKit version, e.g. "0.1.0".
 * @return A static string.
 */
const char* prk_get_version_string(void);

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* PROOFRUNKIT_PRK_H */
