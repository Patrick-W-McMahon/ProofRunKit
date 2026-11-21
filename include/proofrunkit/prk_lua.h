/**
 * @file prk_lua.h
 * @brief C ABI of the embedded, sandboxed Lua 5.4 test-script engine (#9).
 *
 * One PrkLuaScript owns one Lua state for one test script. Loading the script runs its top-level
 * code, which registers tests through the script-facing `prk` table (see docs/lua-tests.md) into a
 * caller-owned PrkRegistry. The registered test bodies call back into the script's Lua state, so
 * the PrkLuaScript must outlive every run of that registry.
 *
 * Sandbox: a memory-limited allocator, an instruction budget enforced by a count hook (reset for
 * every test body), text-only chunks (no precompiled bytecode), and no io, os, package, debug,
 * dofile, loadfile, require, or string.dump. Script text is passed in by the caller, so this
 * module performs no file IO; `print` goes to the caller's output callback.
 */
#ifndef PROOFRUNKIT_PRK_LUA_H
#define PROOFRUNKIT_PRK_LUA_H

#include <stddef.h>
#include <stdint.h>

#include "proofrunkit/prk.h"

#ifdef __cplusplus
extern "C" {
#endif

/** @brief Default memory limit for one script's Lua state: 64 MiB. */
#define PRK_LUA_DEFAULT_MEMORY_LIMIT_BYTES ((size_t)64 * 1024 * 1024)
/** @brief Default instruction budget for loading a script and for each test body: 200 million. */
#define PRK_LUA_DEFAULT_INSTRUCTION_LIMIT ((uint64_t)200000000)
/** @brief Recommended capacity for error-message buffers passed to prk_load_lua_test_script(). */
#define PRK_LUA_ERROR_CAPACITY 1024

/** @brief Resource limits for one script. A zero field selects the default. */
typedef struct PrkLuaLimits {
    size_t memory_limit_bytes;  /**< Maximum bytes the Lua state may hold at once. */
    uint64_t instruction_limit; /**< Maximum Lua VM instructions for the load and for each test body. */
} PrkLuaLimits;

/** @brief Where the script's `print` output goes. A NULL @p write_text discards it. */
typedef struct PrkLuaOutput {
    /** Receives @p length bytes of text (not NUL-terminated). */
    void (*write_text)(void* output_state, const char* text, size_t length);
    void* output_state; /**< Passed back to @p write_text unchanged; may be NULL. Not owned. */
} PrkLuaOutput;

/** @brief Opaque sandboxed Lua state for one test script. */
typedef struct PrkLuaScript PrkLuaScript;

/**
 * @brief Creates an empty sandboxed Lua state.
 * @param limits Resource limits; NULL selects the defaults.
 * @param output Destination for `print`; NULL discards printed text.
 * @return The new script state, or NULL if memory could not be allocated. Free with
 *         prk_destroy_lua_script().
 */
PrkLuaScript* prk_create_lua_script(const PrkLuaLimits* limits, const PrkLuaOutput* output);

/**
 * @brief Closes the Lua state and frees the script. Passing NULL does nothing.
 *
 * Any registry that holds tests from this script must not be run afterwards.
 *
 * @param script Script created by prk_create_lua_script().
 */
void prk_destroy_lua_script(PrkLuaScript* script);

/**
 * @brief Compiles and runs a Lua test script, registering its tests into @p registry.
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
 *        error, limit exceeded, duplicate test name); may be NULL. Untouched on success.
 * @param error_capacity Size of @p out_error in bytes.
 * @return PRK_STATUS_OK, PRK_STATUS_INVALID_ARGUMENT (NULL pointer or script already loaded),
 *         PRK_STATUS_SCRIPT_ERROR, PRK_STATUS_DUPLICATE_NAME, or PRK_STATUS_OUT_OF_MEMORY.
 * Side effects: runs script code in the sandbox; writes printed text to the output callback.
 */
PrkStatus prk_load_lua_test_script(PrkLuaScript* script, PrkRegistry* registry, const char* chunk_name,
                                   const char* source, size_t source_length, char* out_error, size_t error_capacity);

/**
 * @brief Counts the tests the script has registered.
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
