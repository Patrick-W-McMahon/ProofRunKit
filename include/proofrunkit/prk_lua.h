/**
 * @file prk_lua.h
 * @brief C ABI of the embedded, sandboxed Lua 5.4 test-script engine (#9, test API #12).
 *
 * One PrkLuaScript owns one Lua state for one test script. Loading the script runs its top-level
 * code, which registers suites, hooks, and tests through the script-facing `prk` table (see
 * docs/lua-tests.md) into a caller-owned PrkRegistry. The registered test bodies call back into the
 * script's Lua state, so the PrkLuaScript must outlive every run of that registry.
 *
 * Sandbox: a memory-limited allocator, an instruction budget enforced by a count hook (reset for
 * every test), an optional per-test wall-clock timeout, text-only chunks (no precompiled bytecode),
 * and no io, os, package, debug, dofile, loadfile, require, or string.dump. Script text is passed in
 * by the caller, and every other capability (print, clock, read-only file checks) comes from the
 * caller's PrkScriptHost, so this module performs no IO of its own.
 */
#ifndef PROOFRUNKIT_PRK_LUA_H
#define PROOFRUNKIT_PRK_LUA_H

#include <stddef.h>
#include <stdint.h>

#include "proofrunkit/prk.h"
#include "proofrunkit/prk_script.h"

#ifdef __cplusplus
extern "C" {
#endif

/** @brief Default memory limit for one script's Lua state: 64 MiB. */
#define PRK_LUA_DEFAULT_MEMORY_LIMIT_BYTES PRK_SCRIPT_DEFAULT_MEMORY_LIMIT_BYTES
/** @brief Default instruction budget for loading a script and for each test: 200 million. */
#define PRK_LUA_DEFAULT_INSTRUCTION_LIMIT PRK_SCRIPT_DEFAULT_INSTRUCTION_LIMIT
/** @brief Recommended capacity for error-message buffers passed to prk_load_lua_test_script(). */
#define PRK_LUA_ERROR_CAPACITY PRK_SCRIPT_ERROR_CAPACITY

/** @brief Resource limits for one Lua script (the engine-neutral PrkScriptLimits). */
typedef PrkScriptLimits PrkLuaLimits;

/** @brief Opaque sandboxed Lua state for one test script. */
typedef struct PrkLuaScript PrkLuaScript;

/**
 * @brief Creates an empty sandboxed Lua state.
 * @param limits Resource limits; NULL selects the defaults.
 * @param host Services granted to the script (copied); NULL grants none (print output is discarded,
 *        timeouts and file checks are unavailable).
 * @param settings Test selection by name and tags, and the default per-test timeout (copied); NULL
 *        selects every test with no timeout.
 * @return The new script state, or NULL if memory could not be allocated. Free with
 *         prk_destroy_lua_script().
 */
PrkLuaScript* prk_create_lua_script(const PrkLuaLimits* limits, const PrkScriptHost* host,
                                    const PrkScriptRunSettings* settings);

/**
 * @brief Closes the Lua state and frees the script. Passing NULL does nothing.
 *
 * Any registry that holds tests from this script must not be run afterwards.
 *
 * @param script Script created by prk_create_lua_script().
 */
void prk_destroy_lua_script(PrkLuaScript* script);

/**
 * @brief Compiles and runs a Lua test script, registering its selected tests into @p registry.
 *
 * Only text chunks are accepted. The top-level code runs under the instruction budget and memory
 * limit. On failure, tests the script registered before the error stay in @p registry but the
 * caller should treat the script as not loaded.
 *
 * @param script Script state from prk_create_lua_script(); must not have been loaded before.
 * @param registry Registry that receives the script's tests.
 * @param chunk_name Name used in error messages and tracebacks, e.g. the script path.
 * @param source Script text (need not be NUL-terminated).
 * @param source_length Length of @p source in bytes.
 * @param out_error Receives a NUL-terminated message on failure (syntax error with line, runtime
 *        error, limit exceeded, duplicate test name, unbalanced suite); may be NULL. Untouched on success.
 * @param error_capacity Size of @p out_error in bytes.
 * @return PRK_STATUS_OK, PRK_STATUS_INVALID_ARGUMENT (NULL pointer or script already loaded),
 *         PRK_STATUS_SCRIPT_ERROR, PRK_STATUS_DUPLICATE_NAME, or PRK_STATUS_OUT_OF_MEMORY.
 * Side effects: runs script code in the sandbox; writes printed text to the host.
 */
PrkStatus prk_load_lua_test_script(PrkLuaScript* script, PrkRegistry* registry, const char* chunk_name,
                                   const char* source, size_t source_length, char* out_error, size_t error_capacity);

/**
 * @brief Counts the tests the script registered (tests filtered out by the run settings are not counted).
 * @param script Script to inspect; NULL counts as zero.
 * @return Number of tests registered by prk.add_test().
 */
size_t prk_count_lua_script_tests(const PrkLuaScript* script);

/**
 * @brief Returns the bytes currently held by the script's Lua state.
 * @param script Script to inspect; NULL returns 0.
 * @return Bytes allocated through the memory-limited allocator.
 */
size_t prk_get_lua_script_memory_in_use(const PrkLuaScript* script);

/**
 * @brief Returns the Lua version compiled in, e.g. "Lua 5.4.9".
 * @return A static string.
 */
const char* prk_get_lua_version_string(void);

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* PROOFRUNKIT_PRK_LUA_H */
