/**
 * @file prk_js.h
 * @brief C ABI of the embedded, sandboxed QuickJS-ng JavaScript test-script engine (#11).
 *
 * The JavaScript twin of prk_lua.h: one PrkJsScript owns one QuickJS runtime and context for one
 * test script. Loading the script runs its top-level code, which registers suites, hooks, and tests
 * through the global `prk` object (the camelCase twin of the Lua `prk` table, see docs/js-tests.md)
 * into a caller-owned PrkRegistry. The registered test bodies call back into the script's context,
 * so the PrkJsScript must outlive every run of that registry.
 *
 * Sandbox: a runtime memory limit, an operation budget and optional wall-clock timeout enforced by
 * the interrupt handler (the resulting error cannot be caught by try/catch), a bounded JS stack, no
 * `std` / `os` modules (quickjs-libc is not compiled in), no module loader, and no Atomics.wait
 * blocking. Script text is passed in by the caller, and every other capability (print, clock,
 * read-only file checks) comes from the caller's PrkScriptHost.
 */
#ifndef PROOFRUNKIT_PRK_JS_H
#define PROOFRUNKIT_PRK_JS_H

#include <stddef.h>
#include <stdint.h>

#include "proofrunkit/prk.h"
#include "proofrunkit/prk_script.h"

#ifdef __cplusplus
extern "C" {
#endif

/** @brief Maximum native stack the JS engine may use for nested calls: 512 KiB. */
#define PRK_JS_STACK_LIMIT_BYTES ((size_t)512 * 1024)

/** @brief Opaque sandboxed QuickJS runtime and context for one test script. */
typedef struct PrkJsScript PrkJsScript;

/**
 * @brief Creates an empty sandboxed JavaScript context.
 * @param limits Resource limits; NULL selects the defaults (64 MiB, 200 million operations).
 * @param host Services granted to the script (copied); NULL grants none.
 * @param settings Test selection and default per-test timeout (copied); NULL selects all tests.
 * @return The new script state, or NULL if memory could not be allocated. Free with prk_destroy_js_script().
 */
PrkJsScript* prk_create_js_script(const PrkScriptLimits* limits, const PrkScriptHost* host,
                                  const PrkScriptRunSettings* settings);

/**
 * @brief Frees the context, the runtime, and the script. Passing NULL does nothing.
 * @param script Script created by prk_create_js_script(). Registries holding its tests must not run afterwards.
 */
void prk_destroy_js_script(PrkJsScript* script);

/**
 * @brief Compiles and runs a JavaScript test script (a classic script, not a module), registering its
 *        selected tests into @p registry.
 * @param script Script state from prk_create_js_script(); must not have been loaded before.
 * @param registry Registry that receives the script's tests.
 * @param chunk_name File name used in stack traces and failure locations, e.g. the script path.
 * @param source Script text (need not be NUL-terminated).
 * @param source_length Length of @p source in bytes.
 * @param out_error Receives a NUL-terminated message on failure; may be NULL. Untouched on success.
 * @param error_capacity Size of @p out_error in bytes.
 * @return PRK_STATUS_OK, PRK_STATUS_INVALID_ARGUMENT, PRK_STATUS_SCRIPT_ERROR, PRK_STATUS_DUPLICATE_NAME,
 *         or PRK_STATUS_OUT_OF_MEMORY.
 * Side effects: runs script code in the sandbox; writes printed text to the host.
 */
PrkStatus prk_load_js_test_script(PrkJsScript* script, PrkRegistry* registry, const char* chunk_name,
                                  const char* source, size_t source_length, char* out_error, size_t error_capacity);

/**
 * @brief Counts the tests the script registered (tests filtered out by the run settings are not counted).
 * @param script Script to inspect; NULL counts as zero.
 * @return Number of registered tests.
 */
size_t prk_count_js_script_tests(const PrkJsScript* script);

/**
 * @brief Returns the bytes currently allocated by the script's runtime.
 * @param script Script to inspect; NULL returns 0.
 * @return Allocated bytes as reported by QuickJS.
 */
size_t prk_get_js_script_memory_in_use(const PrkJsScript* script);

/**
 * @brief Returns the JavaScript engine compiled in, e.g. "QuickJS-ng 0.18.0".
 * @return A static string.
 */
const char* prk_get_js_engine_version_string(void);

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* PROOFRUNKIT_PRK_JS_H */
