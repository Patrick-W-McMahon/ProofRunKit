/**
 * @file prk_js_internal.h
 * @brief Private layout of PrkJsScript shared by the JavaScript engine sources (#11).
 *
 * Must not be included outside src/script/. All state lives in the PrkJsScript, reachable from the
 * QuickJS context through JS_GetContextOpaque().
 */
#ifndef PROOFRUNKIT_PRK_JS_INTERNAL_H
#define PROOFRUNKIT_PRK_JS_INTERNAL_H

#include "prk_test_plan.h"
#include "proofrunkit/prk_js.h"
/* The vendored header is not ProofRunKit code: keep MSVC's /W4 /WX from failing on its inline functions. */
#if defined(_MSC_VER) && !defined(__clang__)
#pragma warning(push, 1)
#endif
#include "quickjs.h"
#if defined(_MSC_VER) && !defined(__clang__)
#pragma warning(pop)
#endif

/** @brief Operations QuickJS runs between two interrupt-handler calls (JS_INTERRUPT_COUNTER_INIT upstream). */
#define PRK_JS_OPERATIONS_PER_INTERRUPT 10000

struct PrkJsScript {
    JSRuntime* runtime;            /**< The script's own runtime (memory limit, interrupt handler). */
    JSContext* context;            /**< The script's only context. */
    size_t memory_limit;           /**< Runtime memory limit in bytes. */
    uint64_t instruction_limit;    /**< Operation budget for the load and for each test. */
    uint64_t operations_used;      /**< Operations counted since the budget was last reset. */
    uint64_t deadline_ms;          /**< Host-clock time the running test must finish by; 0 = none. */
    uint64_t timeout_ms;           /**< Timeout that produced @p deadline_ms. */
    int is_over_budget;            /**< Set once the budget is exhausted (sticky until the next reset). */
    int is_past_deadline;          /**< Set once the deadline passed (sticky until the next test). */
    PrkScriptHost host;            /**< print, clock, and file services. */
    PrkTestPlan* plan;             /**< Suites, hooks, and tests of this script. */
    PrkRegistry* loading_registry; /**< Non-NULL only while the top-level code runs. */
    PrkStatus registration_status; /**< Registry error raised from prk.addTest() during the load. */
    int is_running_test;           /**< Set while a test, hook, or body runs. */
    int is_loaded;                 /**< Set after the first prk_load_js_test_script() call. */
    JSValue* functions;            /**< Function slots; PrkScriptFunction is an index. */
    size_t function_count;         /**< Used slots. */
    size_t function_capacity;      /**< Allocated slots. */
};

/**
 * @brief Returns the PrkJsScript that owns @p context.
 * @param context The script's context.
 * @return The owning script.
 */
PrkJsScript* prk_find_js_script(JSContext* context);

/**
 * @brief Stores a function in a new slot (taking a new reference).
 * @param script Script that owns the slot.
 * @param function Function value (not consumed).
 * @param out_function Receives the slot index.
 * @return 1 on success, 0 when memory runs out.
 */
int prk_store_js_function(PrkJsScript* script, JSValueConst function, PrkScriptFunction* out_function);

/**
 * @brief Creates the global `prk` object, `print`, `console.log`, and the assertion prelude.
 * @param context Context to populate.
 * @return 0 on success, -1 on failure (exception pending).
 */
int prk_open_js_test_api(JSContext* context);

/**
 * @brief Adds prk.runProcess to the prk object (prk_js_process.c, #12/#13).
 * @param context Context of the script. @param prk The prk object.
 * @return 0, or -1 with an exception pending.
 */
int prk_add_js_process_api(JSContext* context, JSValueConst prk);

#endif /* PROOFRUNKIT_PRK_JS_INTERNAL_H */
