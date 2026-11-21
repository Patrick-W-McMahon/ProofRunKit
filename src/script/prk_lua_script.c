/**
 * @file prk_lua_script.c
 * @brief Lifecycle of a sandboxed Lua test script and the engine calls its test plan uses (#9, #12).
 *
 * Implements the public C ABI in include/proofrunkit/prk_lua.h. Performs no file IO: the caller
 * passes script text in. Lua errors never escape this module: every call into script code goes
 * through lua_pcall, so no longjmp crosses the caller's C or C++ frames.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "lauxlib.h"
#include "prk_lua_internal.h"

const char* prk_get_lua_version_string(void) {
    return LUA_RELEASE;
}

/** @brief PrkScriptEngineCalls.begin_test: fresh instruction budget and deadline for one test. */
static void prk_begin_lua_test(void* engine, uint64_t timeout_ms) {
    PrkLuaScript* const script = (PrkLuaScript*)engine;
    prk_reset_lua_instruction_budget(script);
    prk_start_lua_deadline(script, timeout_ms);
}

/** @brief PrkScriptEngineCalls.call_function: calls a registry-referenced Lua function under the sandbox. */
static PrkScriptCallResult prk_call_lua_function(void* engine, PrkScriptFunction function, char* out_message,
                                                 size_t message_capacity) {
    PrkLuaScript* const script = (PrkLuaScript*)engine;
    lua_State* const state = script->state;
    const int top = lua_gettop(state);
    if (!lua_checkstack(state, 4)) {
        (void)snprintf(out_message, message_capacity, "%s", "not enough memory to call the Lua function");
        return PRK_SCRIPT_CALL_FAILED;
    }
    lua_pushcfunction(state, &prk_add_lua_error_traceback);
    lua_rawgeti(state, LUA_REGISTRYINDEX, (lua_Integer)function);
    script->is_running_test = 1;
    const int status = lua_pcall(state, 0, 0, top + 1);
    script->is_running_test = 0;
    PrkScriptCallResult result = PRK_SCRIPT_CALL_OK;
    if (status != LUA_OK) {
        const int is_skip = status != LUA_ERRMEM && prk_is_lua_error_object(state, -1, PRK_LUA_SKIP_TYPE);
        result = is_skip ? PRK_SCRIPT_CALL_SKIPPED : PRK_SCRIPT_CALL_FAILED;
        prk_describe_lua_error(script, status, out_message, message_capacity);
    }
    lua_settop(state, top);
    if (status != LUA_OK) {
        /* Reclaim what a failed call left behind so the next test starts with headroom. */
        (void)lua_gc(state, LUA_GCCOLLECT);
    }
    return result;
}

/** @brief PrkScriptEngineCalls.release_function: drops the registry reference. */
static void prk_release_lua_function(void* engine, PrkScriptFunction function) {
    PrkLuaScript* const script = (PrkLuaScript*)engine;
    luaL_unref(script->state, LUA_REGISTRYINDEX, (int)function);
}

PrkLuaScript* prk_create_lua_script(const PrkLuaLimits* limits, const PrkScriptHost* host,
                                    const PrkScriptRunSettings* settings) {
    PrkLuaScript* const script = (PrkLuaScript*)calloc(1, sizeof(PrkLuaScript));
    if (script == NULL) {
        return NULL;
    }
    script->memory_limit = limits != NULL && limits->memory_limit_bytes != 0 ? limits->memory_limit_bytes
                                                                             : PRK_LUA_DEFAULT_MEMORY_LIMIT_BYTES;
    script->instruction_limit = limits != NULL && limits->instruction_limit != 0 ? limits->instruction_limit
                                                                                 : PRK_LUA_DEFAULT_INSTRUCTION_LIMIT;
    if (host != NULL) {
        script->host = *host;
    }
    script->registration_status = PRK_STATUS_OK;
    script->state = lua_newstate(&prk_allocate_lua_memory, script);
    if (script->state == NULL) {
        free(script);
        return NULL;
    }
    const PrkScriptEngineCalls engine_calls = {&prk_begin_lua_test, &prk_call_lua_function, &prk_release_lua_function,
                                               script};
    script->plan = prk_create_test_plan(&engine_calls, settings);
    lua_pushcfunction(script->state, &prk_open_lua_sandbox_libraries);
    const int libraries_status = lua_pcall(script->state, 0, 0, 0);
    lua_pushcfunction(script->state, &prk_open_lua_test_api);
    const int api_status = libraries_status == LUA_OK ? lua_pcall(script->state, 0, 0, 0) : libraries_status;
    if (api_status != LUA_OK || script->plan == NULL) {
        prk_destroy_lua_script(script);
        return NULL;
    }
    return script;
}

void prk_destroy_lua_script(PrkLuaScript* script) {
    if (script == NULL) {
        return;
    }
    prk_destroy_test_plan(script->plan); /* Releases registry references, so it runs before lua_close. */
    if (script->state != NULL) {
        lua_close(script->state);
    }
    free(script);
}

/** @brief Builds "@<chunk_name>" so Lua reports locations as "<chunk_name>:<line>:". Caller frees. */
static char* prk_make_lua_chunk_name(const char* chunk_name) {
    const size_t length = strlen(chunk_name);
    char* const decorated = (char*)malloc(length + 2);
    if (decorated != NULL) {
        decorated[0] = '@';
        memcpy(decorated + 1, chunk_name, length + 1);
    }
    return decorated;
}

/** @brief Runs the compiled chunk at the top of the stack with the traceback handler. */
static int prk_run_lua_chunk(PrkLuaScript* script) {
    lua_State* const state = script->state;
    const int chunk_index = lua_gettop(state);
    lua_pushcfunction(state, &prk_add_lua_error_traceback);
    lua_insert(state, chunk_index);
    prk_reset_lua_instruction_budget(script);
    const int status = lua_pcall(state, 0, 0, chunk_index);
    lua_remove(state, chunk_index);
    return status;
}

PrkStatus prk_load_lua_test_script(PrkLuaScript* script, PrkRegistry* registry, const char* chunk_name,
                                   const char* source, size_t source_length, char* out_error, size_t error_capacity) {
    if (script == NULL || registry == NULL || chunk_name == NULL || source == NULL || script->is_loaded) {
        return PRK_STATUS_INVALID_ARGUMENT;
    }
    script->is_loaded = 1;
    char* const decorated_name = prk_make_lua_chunk_name(chunk_name);
    if (decorated_name == NULL || !lua_checkstack(script->state, 4)) {
        free(decorated_name);
        return PRK_STATUS_OUT_OF_MEMORY;
    }
    const int top = lua_gettop(script->state);
    /* Mode "t": precompiled bytecode is rejected because it can crash or escape the VM. */
    int status = luaL_loadbufferx(script->state, source, source_length, decorated_name, "t");
    free(decorated_name);
    if (status == LUA_OK) {
        script->loading_registry = registry;
        status = prk_run_lua_chunk(script);
        script->loading_registry = NULL;
    }
    if (status == LUA_OK && prk_count_open_test_suites(script->plan) != 0) {
        lua_settop(script->state, top);
        if (out_error != NULL && error_capacity > 0) {
            (void)snprintf(out_error, error_capacity, "%s", "a prk.describe() suite was left open");
        }
        return PRK_STATUS_SCRIPT_ERROR;
    }
    if (status == LUA_OK) {
        lua_settop(script->state, top);
        return PRK_STATUS_OK;
    }
    prk_describe_lua_error(script, status, out_error, error_capacity);
    lua_settop(script->state, top);
    return script->registration_status != PRK_STATUS_OK ? script->registration_status : PRK_STATUS_SCRIPT_ERROR;
}

size_t prk_count_lua_script_tests(const PrkLuaScript* script) {
    return script == NULL ? 0 : prk_count_planned_tests(script->plan);
}

size_t prk_get_lua_script_memory_in_use(const PrkLuaScript* script) {
    return script == NULL ? 0 : script->memory_in_use;
}
