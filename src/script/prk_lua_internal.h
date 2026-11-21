/**
 * @file prk_lua_internal.h
 * @brief Private layout of PrkLuaScript and the sandbox helpers shared by the Lua engine sources (#9).
 *
 * Must not be included outside src/script/. Nothing here is global: every piece of state lives in
 * the PrkLuaScript, which Lua hands back through the allocator's user-data pointer.
 */
#ifndef PROOFRUNKIT_PRK_LUA_INTERNAL_H
#define PROOFRUNKIT_PRK_LUA_INTERNAL_H

#include "lua.h"
#include "proofrunkit/prk_lua.h"

/** @brief VM instructions between two instruction-budget checks. */
#define PRK_LUA_HOOK_INTERVAL 1000

/** @brief One test registered by prk.add_test(); the address is the PrkTestCase user_state. */
typedef struct PrkLuaTest {
    PrkLuaScript* script; /**< Owning script. */
    int function_ref;     /**< Registry reference to the Lua test body. */
} PrkLuaTest;

struct PrkLuaScript {
    lua_State* state;              /**< The script's own Lua state. */
    size_t memory_in_use;          /**< Bytes currently allocated by @p state. */
    size_t memory_limit;           /**< Allocation requests beyond this fail. */
    uint64_t instruction_limit;    /**< Budget for the load and for each test body. */
    uint64_t instructions_used;    /**< Instructions counted since the budget was last reset. */
    PrkLuaOutput output;           /**< Destination for print(). */
    PrkRegistry* loading_registry; /**< Non-NULL only while the top-level chunk runs. */
    PrkStatus registration_status; /**< Registry error raised from prk.add_test() during the load. */
    PrkTestContext* running_test;  /**< Non-NULL only while a test body runs. */
    PrkLuaTest** tests;            /**< Tests registered by this script (each separately allocated). */
    size_t test_count;             /**< Number of entries in @p tests. */
    size_t test_capacity;          /**< Allocated entries in @p tests. */
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
 * @brief lua_CFunction that opens the safe standard libraries and removes ambient access.
 *
 * Opens base, coroutine, table, string, math, and utf8; removes dofile, loadfile, and
 * string.dump; replaces print (output callback) and load (text chunks only). Run it with lua_pcall.
 * @return 0 (no results).
 */
int prk_open_lua_sandbox_libraries(lua_State* state);

/**
 * @brief lua_pcall message handler: adds a traceback to errors, keeps the stop marker unchanged.
 * @return 1 (the handled error value).
 */
int prk_add_lua_error_traceback(lua_State* state);

/**
 * @brief Returns the marker raised by expectations and skips to stop a test body without a second report.
 * @return A light-userdata key unique to this module.
 */
void* prk_get_lua_stop_marker(void);

/**
 * @brief Copies the error at the top of the stack (or a limit message) into @p out_text.
 * @param script Script whose limits explain memory errors.
 * @param pcall_status Status returned by lua_pcall or a load function.
 * @param out_text Destination buffer; may be NULL.
 * @param capacity Size of @p out_text.
 */
void prk_describe_lua_error(const PrkLuaScript* script, int pcall_status, char* out_text, size_t capacity);

/**
 * @brief lua_CFunction that creates the script-facing `prk` table (add_test, expect_true, ...).
 * @return 0 (no results); the table is stored as the global `prk`.
 */
int prk_open_lua_test_api(lua_State* state);

/**
 * @brief PrkTestBody that runs one Lua test registered by prk.add_test().
 * @param context Core test context.
 * @param test_state The PrkLuaTest.
 */
void prk_run_lua_test_body(PrkTestContext* context, void* test_state);

#endif /* PROOFRUNKIT_PRK_LUA_INTERNAL_H */
