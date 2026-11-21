/**
 * @file prk_lua_api.c
 * @brief The script-facing `prk` table for Lua test scripts (#9; the minimal subset of #12).
 *
 *   prk.add_test(name, body)                   register a test (only while the script loads)
 *   prk.expect_true(value [, message])         fail and stop the test unless value is truthy
 *   prk.expect_equal(actual, expected [, msg]) fail and stop the test unless actual == expected
 *   prk.fail_test(message)                     fail and stop the test
 *   prk.skip_test(reason)                      skip and stop the test
 *   prk.version, prk.lua_version               version strings
 *
 * A plain Lua error (error(), assert()) inside a body also fails the test, with a traceback.
 * Must not grant file, process, or environment access; those arrive with #12 and #13.
 */
#include <stdlib.h>

#include "lauxlib.h"
#include "prk_lua_internal.h"

/** @brief Returns the running test's context, or raises a Lua error naming @p function_name. */
static PrkTestContext* prk_require_running_lua_test(lua_State* state, const char* function_name) {
    PrkTestContext* const context = prk_find_lua_script(state)->running_test;
    if (context == NULL) {
        luaL_error(state, "%s can only be called inside a test body", function_name);
    }
    return context;
}

/** @brief Fails the running test with "<where>: <message>" and stops the body. Does not return. */
static int prk_stop_lua_test_with_failure(lua_State* state, PrkTestContext* context, const char* message) {
    luaL_where(state, 1);
    lua_pushstring(state, message);
    lua_concat(state, 2);
    prk_fail_test(context, lua_tostring(state, -1));
    lua_pushlightuserdata(state, prk_get_lua_stop_marker());
    return lua_error(state);
}

/** @brief Appends a test pointer to the script's list, growing it; returns 0 when memory runs out. */
static int prk_reserve_lua_test_slot(PrkLuaScript* script) {
    if (script->test_count < script->test_capacity) {
        return 1;
    }
    const size_t capacity = script->test_capacity == 0 ? 8 : script->test_capacity * 2;
    PrkLuaTest** const grown = (PrkLuaTest**)realloc(script->tests, capacity * sizeof(PrkLuaTest*));
    if (grown == NULL) {
        return 0;
    }
    script->tests = grown;
    script->test_capacity = capacity;
    return 1;
}

/** @brief prk.add_test(name, body). */
static int prk_lua_add_test(lua_State* state) {
    PrkLuaScript* const script = prk_find_lua_script(state);
    const char* const name = luaL_checkstring(state, 1);
    luaL_checktype(state, 2, LUA_TFUNCTION);
    if (script->loading_registry == NULL) {
        return luaL_error(state, "prk.add_test can only be called while the script loads, not inside a test");
    }
    if (!prk_reserve_lua_test_slot(script)) {
        return luaL_error(state, "not enough memory to register test '%s'", name);
    }
    lua_pushvalue(state, 2);
    const int function_ref = luaL_ref(state, LUA_REGISTRYINDEX);
    PrkLuaTest* const test = (PrkLuaTest*)malloc(sizeof(PrkLuaTest));
    if (test == NULL) {
        luaL_unref(state, LUA_REGISTRYINDEX, function_ref);
        return luaL_error(state, "not enough memory to register test '%s'", name);
    }
    test->script = script;
    test->function_ref = function_ref;
    const PrkTestCase test_case = {name, &prk_run_lua_test_body, test};
    const PrkStatus status = prk_register_test_case(script->loading_registry, &test_case);
    if (status != PRK_STATUS_OK) {
        free(test);
        luaL_unref(state, LUA_REGISTRYINDEX, function_ref);
        script->registration_status = status;
        return luaL_error(state, "could not register test '%s': %s", name, prk_get_status_name(status));
    }
    script->tests[script->test_count] = test;
    script->test_count += 1;
    return 0;
}

/** @brief prk.expect_true(value [, message]). */
static int prk_lua_expect_true(lua_State* state) {
    PrkTestContext* const context = prk_require_running_lua_test(state, "prk.expect_true");
    luaL_checkany(state, 1);
    if (lua_toboolean(state, 1)) {
        return 0;
    }
    return prk_stop_lua_test_with_failure(state, context, luaL_optstring(state, 2, "expected a true value"));
}

/** @brief prk.expect_equal(actual, expected [, message]). */
static int prk_lua_expect_equal(lua_State* state) {
    PrkTestContext* const context = prk_require_running_lua_test(state, "prk.expect_equal");
    luaL_checkany(state, 2);
    if (lua_compare(state, 1, 2, LUA_OPEQ)) {
        return 0;
    }
    const char* const prefix = luaL_optstring(state, 3, "values differ");
    const char* const actual = luaL_tolstring(state, 1, NULL);
    const char* const expected = luaL_tolstring(state, 2, NULL);
    const char* const message = lua_pushfstring(state, "%s: expected %s, got %s", prefix, expected, actual);
    return prk_stop_lua_test_with_failure(state, context, message);
}

/** @brief prk.fail_test(message). */
static int prk_lua_fail_test(lua_State* state) {
    PrkTestContext* const context = prk_require_running_lua_test(state, "prk.fail_test");
    return prk_stop_lua_test_with_failure(state, context, luaL_optstring(state, 1, "test failed"));
}

/** @brief prk.skip_test(reason). */
static int prk_lua_skip_test(lua_State* state) {
    PrkTestContext* const context = prk_require_running_lua_test(state, "prk.skip_test");
    prk_skip_test(context, luaL_optstring(state, 1, "skipped"));
    lua_pushlightuserdata(state, prk_get_lua_stop_marker());
    return lua_error(state);
}

int prk_open_lua_test_api(lua_State* state) {
    static const luaL_Reg api_functions[] = {
        {"add_test", &prk_lua_add_test},         {"expect_true", &prk_lua_expect_true},
        {"expect_equal", &prk_lua_expect_equal}, {"fail_test", &prk_lua_fail_test},
        {"skip_test", &prk_lua_skip_test},       {NULL, NULL},
    };
    luaL_newlib(state, api_functions);
    lua_pushstring(state, prk_get_version_string());
    lua_setfield(state, -2, "version");
    lua_pushliteral(state, LUA_RELEASE);
    lua_setfield(state, -2, "lua_version");
    lua_setglobal(state, "prk");
    return 0;
}
