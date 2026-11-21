/**
 * @file prk_lua_internal.h
 * @brief Private layout of PrkLuaScript and the sandbox helpers shared by the Lua engine sources (#9, #12).
 *
 * Must not be included outside src/script/. Nothing here is global: every piece of state lives in
 * the PrkLuaScript, which Lua hands back through the allocator's user-data pointer.
 */
#ifndef PROOFRUNKIT_PRK_LUA_INTERNAL_H
#define PROOFRUNKIT_PRK_LUA_INTERNAL_H

#include "lua.h"
#include "prk_test_plan.h"
#include "proofrunkit/prk_lua.h"

/** @brief VM instructions between two instruction-budget and timeout checks. */
#define PRK_LUA_HOOK_INTERVAL 1000

/** @brief Registry name of the metatable for assertion failures raised by prk.expect_* and prk.fail_test. */
#define PRK_LUA_ASSERTION_ERROR_TYPE "prk.AssertionError"
/** @brief Registry name of the metatable for the error object raised by prk.skip_test. */
#define PRK_LUA_SKIP_TYPE "prk.Skip"

struct PrkLuaScript {
    lua_State* state;              /**< The script's own Lua state. */
    size_t memory_in_use;          /**< Bytes currently allocated by @p state. */
    size_t memory_limit;           /**< Allocation requests beyond this fail. */
    uint64_t instruction_limit;    /**< Budget for the load and for each test. */
    uint64_t instructions_used;    /**< Instructions counted since the budget was last reset. */
    uint64_t deadline_ms;          /**< Host-clock time the running test must finish by; 0 = none. */
    uint64_t timeout_ms;           /**< Timeout that produced @p deadline_ms (for the message). */
    int is_past_deadline;          /**< Set once the running test has timed out. */
    PrkScriptHost host;            /**< print, clock, and file services. */
    PrkTestPlan* plan;             /**< Suites, hooks, and tests of this script. */
    PrkRegistry* loading_registry; /**< Non-NULL only while the top-level chunk runs. */
    PrkStatus registration_status; /**< Registry error raised from prk.add_test() during the load. */
    int is_running_test;           /**< Set while a test, hook, or body runs. */
    int is_loaded;                 /**< Set after the first prk_load_lua_test_script() call. */
};

/**
 * @brief Returns the PrkLuaScript that owns @p state (stored as the allocator user data).
 * @param state Any thread of the script's Lua state.
 * @return The owning script.
 */
PrkLuaScript* prk_find_lua_script(lua_State* state);

/**
 * @brief Lua allocator that enforces PrkLuaScript.memory_limit. Signature of lua_Alloc.
 * Side effects: updates memory_in_use.
 */
void* prk_allocate_lua_memory(void* script_state, void* block, size_t old_size, size_t new_size);

/**
 * @brief Resets the instruction budget and installs the count hook on the main state.
 * @param script Script whose budget starts again from zero.
 */
void prk_reset_lua_instruction_budget(PrkLuaScript* script);

/**
 * @brief Starts the wall-clock deadline for one test. Needs host.read_monotonic_milliseconds.
 * @param script Script about to run a test.
 * @param timeout_ms Timeout in milliseconds; 0 (or no host clock) means no deadline.
 */
void prk_start_lua_deadline(PrkLuaScript* script, uint64_t timeout_ms);

/**
 * @brief lua_CFunction that opens the safe standard libraries and removes ambient access.
 *
 * Opens base, coroutine, table, string, math, and utf8; removes dofile, loadfile, and
 * string.dump; replaces print (host output) and load (text chunks only). Run it with lua_pcall.
 * @return 0 (no results).
 */
int prk_open_lua_sandbox_libraries(lua_State* state);

/**
 * @brief lua_pcall message handler: adds a traceback to errors; keeps prk assertion and skip objects unchanged.
 * @return 1 (the handled error value).
 */
int prk_add_lua_error_traceback(lua_State* state);

/**
 * @brief Reports whether the value at @p index is a table with the metatable registered as @p type_name.
 * @return 1 or 0. Does not raise.
 */
int prk_is_lua_error_object(lua_State* state, int index, const char* type_name);

/**
 * @brief Copies the error at the top of the stack (or a limit message) into @p out_text.
 * @param script Script whose limits explain memory errors.
 * @param pcall_status Status returned by lua_pcall or a load function.
 * @param out_text Destination buffer; may be NULL.
 * @param capacity Size of @p out_text.
 */
void prk_describe_lua_error(const PrkLuaScript* script, int pcall_status, char* out_text, size_t capacity);

/**
 * @brief lua_CFunction that creates the script-facing `prk` table and the assertion prelude.
 * @return 0 (no results); the table is stored as the global `prk`.
 */
int prk_open_lua_test_api(lua_State* state);

#endif /* PROOFRUNKIT_PRK_LUA_INTERNAL_H */
