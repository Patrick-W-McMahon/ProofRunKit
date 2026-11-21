/**
 * @file prk_lua_process.c
 * @brief prk.run_process for Lua test scripts (#12 over the #13 spawn layer).
 *
 *   local result = prk.run_process{program = "gcc", args = {"-c", "a.c"}, cwd = "build",
 *                                  env = {LANG = "C"}, inherit_env = true, stdin = "...",
 *                                  timeout_ms = 30000, output_limit = 1048576}
 *   result.ok, result.outcome, result.exit_code, result.signal, result.stdout, result.stderr,
 *   result.stdout_truncated, result.stderr_truncated, result.elapsed_ms, result.error
 *
 * Spawning goes only through the host's PrkProcessRunner; without one the call raises an error. A
 * missing program or a timeout is returned as a result (outcome "failed to start" / "timed out"), never
 * raised, so the test decides; prk.expect_exit_code turns it into a clear failure. The process timeout
 * is capped by the running test's own timeout. No shell is involved.
 */
#include <string.h>

#include "lauxlib.h"
#include "prk_lua_internal.h"

/** @brief Registry name of the userdata that owns a PrkProcessResult until Lua has copied it. */
#define PRK_LUA_PROCESS_RESULT_TYPE "prk.ProcessResult"

/** @brief __gc of the result holder: releases the C buffers even if copying raised an error. */
static int prk_free_lua_process_result(lua_State* state) {
    PrkProcessResult* const result = (PrkProcessResult*)luaL_checkudata(state, 1, PRK_LUA_PROCESS_RESULT_TYPE);
    prk_release_process_result(result);
    return 0;
}

/** @brief Reads an optional positive integer option. */
static uint64_t prk_read_lua_positive_option(lua_State* state, int options, const char* key) {
    const int type = lua_getfield(state, options, key);
    uint64_t value = 0;
    if (type != LUA_TNIL) {
        int is_integer = 0;
        const lua_Integer number = lua_tointegerx(state, -1, &is_integer);
        if (!is_integer || number <= 0) {
            luaL_error(state, "prk.run_process: %s must be a positive integer", key);
        }
        value = (uint64_t)number;
    }
    lua_pop(state, 1);
    return value;
}

/** @brief Reads an optional string option (the string stays alive in the options table). */
static const char* prk_read_lua_string_option(lua_State* state, int options, const char* key, size_t* out_length) {
    const int type = lua_getfield(state, options, key);
    const char* text = NULL;
    if (type == LUA_TSTRING) {
        text = lua_tolstring(state, -1, out_length);
    } else if (type != LUA_TNIL) {
        luaL_error(state, "prk.run_process: %s must be a string", key);
    }
    lua_pop(state, 1);
    return text;
}

/** @brief Rejects option keys this function does not know, so typos are not silently ignored. */
static void prk_check_lua_process_option_keys(lua_State* state, int options) {
    static const char* const known[] = {"program", "args",       "cwd",          "env", "inherit_env",
                                        "stdin",   "timeout_ms", "output_limit", NULL};
    lua_pushnil(state);
    while (lua_next(state, options) != 0) {
        const char* const key = lua_type(state, -2) == LUA_TSTRING ? lua_tostring(state, -2) : NULL;
        int is_known = 0;
        for (size_t index = 0; key != NULL && known[index] != NULL; ++index) {
            is_known |= strcmp(key, known[index]) == 0;
        }
        if (!is_known) {
            luaL_error(state, "prk.run_process: unknown option '%s'", key != NULL ? key : "(non-string key)");
        }
        lua_pop(state, 1);
    }
}

/** @brief Builds a pointer array (userdata, so it is collected on error) from the sequence args. */
static const char** prk_read_lua_arguments(lua_State* state, int options, size_t* out_count) {
    const int type = lua_getfield(state, options, "args");
    if (type == LUA_TNIL) {
        lua_pop(state, 1);
        *out_count = 0;
        return NULL;
    }
    luaL_argcheck(state, type == LUA_TTABLE, 1, "prk.run_process: args must be a sequence of strings");
    const size_t count = (size_t)luaL_len(state, -1);
    const char** const pointers = (const char**)lua_newuserdatauv(state, (count + 1) * sizeof *pointers, 0);
    for (size_t index = 0; index < count; ++index) {
        if (lua_geti(state, -2, (lua_Integer)index + 1) != LUA_TSTRING) {
            luaL_error(state, "prk.run_process: args[%d] must be a string", (int)index + 1);
        }
        pointers[index] = lua_tostring(state, -1); /* Kept alive by the args table. */
        lua_pop(state, 1);
    }
    lua_remove(state, -2); /* Keep the pointer array on the stack; args stays alive in the options table. */
    *out_count = count;
    return pointers;
}

/** @brief Builds "NAME=VALUE" strings from the env table; leaves a holder table and the array on the stack. */
static const char** prk_read_lua_environment(lua_State* state, int options, size_t* out_count) {
    const int type = lua_getfield(state, options, "env");
    if (type == LUA_TNIL) {
        lua_pop(state, 1);
        *out_count = 0;
        return NULL;
    }
    luaL_argcheck(state, type == LUA_TTABLE, 1, "prk.run_process: env must be a table of NAME = value strings");
    const int env = lua_gettop(state);
    lua_newtable(state); /* Holds the formatted strings so their pointers stay valid. */
    const int holder = lua_gettop(state);
    size_t count = 0;
    lua_pushnil(state);
    while (lua_next(state, env) != 0) {
        if (lua_type(state, -2) != LUA_TSTRING || lua_type(state, -1) != LUA_TSTRING || lua_rawlen(state, -2) == 0 ||
            strchr(lua_tostring(state, -2), '=') != NULL) {
            luaL_error(state, "prk.run_process: env must map variable names (without '=') to strings");
        }
        lua_pushfstring(state, "%s=%s", lua_tostring(state, -2), lua_tostring(state, -1));
        lua_rawseti(state, holder, (lua_Integer)++count);
        lua_pop(state, 1);
    }
    const char** const pointers = (const char**)lua_newuserdatauv(state, (count + 1) * sizeof *pointers, 0);
    for (size_t index = 0; index < count; ++index) {
        lua_rawgeti(state, holder, (lua_Integer)index + 1);
        pointers[index] = lua_tostring(state, -1);
        lua_pop(state, 1);
    }
    lua_remove(state, env); /* Stack keeps: holder table, pointer array. */
    *out_count = count;
    return pointers;
}

/** @brief Sets result[key] = string of @p length bytes. */
static void prk_set_lua_bytes_field(lua_State* state, const char* key, const char* bytes, size_t length) {
    lua_pushlstring(state, bytes != NULL ? bytes : "", bytes != NULL ? length : 0);
    lua_setfield(state, -2, key);
}

/** @brief Pushes the Lua result table for @p result. */
static void prk_push_lua_process_result(lua_State* state, const PrkProcessResult* result) {
    lua_createtable(state, 0, 10);
    lua_pushboolean(state, result->outcome == PRK_PROCESS_EXITED && result->exit_code == 0);
    lua_setfield(state, -2, "ok");
    lua_pushstring(state, prk_get_process_outcome_name(result->outcome));
    lua_setfield(state, -2, "outcome");
    if (result->outcome == PRK_PROCESS_EXITED) {
        lua_pushinteger(state, result->exit_code);
        lua_setfield(state, -2, "exit_code");
    }
    if (result->outcome == PRK_PROCESS_SIGNALED) {
        lua_pushinteger(state, result->termination_signal);
        lua_setfield(state, -2, "signal");
    }
    prk_set_lua_bytes_field(state, "stdout", result->standard_output, result->standard_output_length);
    prk_set_lua_bytes_field(state, "stderr", result->standard_error, result->standard_error_length);
    lua_pushboolean(state, result->is_standard_output_truncated);
    lua_setfield(state, -2, "stdout_truncated");
    lua_pushboolean(state, result->is_standard_error_truncated);
    lua_setfield(state, -2, "stderr_truncated");
    lua_pushinteger(state, (lua_Integer)result->elapsed_ms);
    lua_setfield(state, -2, "elapsed_ms");
    if (result->outcome == PRK_PROCESS_FAILED_TO_START) {
        lua_pushstring(state, result->error_message);
        lua_setfield(state, -2, "error");
    }
}

/** @brief prk.run_process(options) -> result table. */
static int prk_lua_run_process(lua_State* state) {
    const PrkLuaScript* const script = prk_find_lua_script(state);
    luaL_checktype(state, 1, LUA_TTABLE);
    if (script->host.process_runner.spawn_process == NULL) {
        return luaL_error(state, "prk.run_process: process spawn is not available in this host");
    }
    prk_check_lua_process_option_keys(state, 1);
    PrkProcessRequest request;
    memset(&request, 0, sizeof request);
    request.executable = prk_read_lua_string_option(state, 1, "program", NULL);
    luaL_argcheck(state, request.executable != NULL && request.executable[0] != '\0', 1,
                  "prk.run_process: program must be a non-empty string");
    request.working_directory = prk_read_lua_string_option(state, 1, "cwd", NULL);
    request.standard_input = prk_read_lua_string_option(state, 1, "stdin", &request.standard_input_length);
    const uint64_t requested_timeout = prk_read_lua_positive_option(state, 1, "timeout_ms");
    request.output_limit_bytes = (size_t)prk_read_lua_positive_option(state, 1, "output_limit");
    lua_getfield(state, 1, "inherit_env");
    request.is_environment_inherited = lua_isnil(state, -1) || lua_toboolean(state, -1);
    lua_pop(state, 1);
    request.arguments = prk_read_lua_arguments(state, 1, &request.argument_count);
    request.environment = prk_read_lua_environment(state, 1, &request.environment_count);
    const uint64_t now = script->host.read_monotonic_milliseconds != NULL
                             ? script->host.read_monotonic_milliseconds(script->host.host_state)
                             : 0;
    request.timeout_ms = prk_cap_script_process_timeout_ms(requested_timeout, script->deadline_ms, now);
    if (request.timeout_ms > PRK_PROCESS_MAX_TIMEOUT_MS) {
        return luaL_error(state, "prk.run_process: timeout_ms must be at most %d", (int)PRK_PROCESS_MAX_TIMEOUT_MS);
    }
    PrkProcessResult* const result = (PrkProcessResult*)lua_newuserdatauv(state, sizeof *result, 0);
    memset(result, 0, sizeof *result);
    luaL_setmetatable(state, PRK_LUA_PROCESS_RESULT_TYPE);
    const PrkStatus status =
        script->host.process_runner.spawn_process(script->host.process_runner.runner_state, &request, result);
    if (status != PRK_STATUS_OK) {
        return luaL_error(state, "prk.run_process: %s", prk_get_status_name(status));
    }
    prk_push_lua_process_result(state, result);
    prk_release_process_result(result);
    return 1;
}

int prk_add_lua_process_api(lua_State* state) {
    luaL_newmetatable(state, PRK_LUA_PROCESS_RESULT_TYPE);
    lua_pushcfunction(state, &prk_free_lua_process_result);
    lua_setfield(state, -2, "__gc");
    lua_pop(state, 1);
    lua_pushcfunction(state, &prk_lua_run_process);
    lua_setfield(state, -2, "run_process");
    return 0;
}
