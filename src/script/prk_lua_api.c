/**
 * @file prk_lua_api.c
 * @brief The script-facing `prk` table for Lua test scripts (#9, test-authoring API #12).
 *
 * Registration (only while the script loads):
 *   prk.add_test(name, body [, options])      options: {tags = {...}, timeout_ms = n, skip = reason}
 *   prk.describe(name, define [, options])    groups tests into a suite; options: {tags = {...}}
 *   prk.before_all / after_all / before_each / after_each(hook)
 * Inside tests and hooks:
 *   prk.expect_*(...)                         assertions (Lua prelude below); they raise prk.AssertionError
 *   prk.fail_test(message), prk.skip_test(reason)
 *   prk.read_clock_ms(), prk.measure_elapsed_ms(fn, ...)
 *   prk.is_file_present(path), prk.read_text_file(path), prk.expect_file_present / expect_file_contains
 *   prk.run_process{...}, prk.expect_exit_code(result, code)   process spawn (#13; prk_lua_process.c)
 *   prk.version, prk.lua_version
 *
 * Failures and skips are error objects (tables with the metatables prk.AssertionError and prk.Skip
 * whose __tostring is the message), so a script can catch them with pcall exactly as a JavaScript test
 * catches a thrown error (#11). A plain Lua error inside a test also fails it, with a traceback.
 * Must not grant ambient file, process, or environment access: files go through the host's read-only
 * callbacks, and processes only through the host's runner (prk.run_process, prk_lua_process.c).
 *
 * Size: over the 400-line review mark because about 160 lines are the Lua assertion prelude, a data table
 * kept beside the C functions it calls.
 */
#include <stdlib.h>
#include <string.h>

#include "lauxlib.h"
#include "prk_lua_internal.h"

/** @brief Most tags one test or suite may carry. */
#define PRK_LUA_MAX_TAGS 32
/** @brief Registry name of the metatable that frees a file buffer if pushing it to Lua fails. */
#define PRK_LUA_FILE_BUFFER_TYPE "prk.FileBuffer"

/* ------------------------------------------------------------------ error objects */

/** @brief Raises an error object of @p type_name carrying @p message. Does not return. */
static int prk_raise_lua_error_object(lua_State* state, const char* type_name, const char* message) {
    lua_createtable(state, 0, 1);
    lua_pushstring(state, message);
    lua_setfield(state, -2, "message");
    luaL_setmetatable(state, type_name);
    return lua_error(state);
}

/** @brief Raises a prk.AssertionError "<where level>: <message>". Does not return. */
static int prk_raise_lua_assertion(lua_State* state, const char* message, int level) {
    luaL_where(state, level);
    lua_pushstring(state, message);
    lua_concat(state, 2);
    return prk_raise_lua_error_object(state, PRK_LUA_ASSERTION_ERROR_TYPE, lua_tostring(state, -1));
}

/** @brief __tostring of the error objects: returns their message. */
static int prk_describe_lua_error_object(lua_State* state) {
    lua_getfield(state, 1, "message");
    if (!lua_isstring(state, -1)) {
        lua_pushliteral(state, "(error object without a message)");
    }
    return 1;
}

/** @brief raise(message, level): handed to the prelude only; raises an assertion at the caller's caller. */
static int prk_lua_raise_assertion(lua_State* state) {
    const char* const message = luaL_checkstring(state, 1);
    const int level = (int)luaL_optinteger(state, 2, 1);
    return prk_raise_lua_assertion(state, message, level);
}

/** @brief Raises a usage error unless a test, hook, or body is running. */
static void prk_require_running_lua_test(lua_State* state, const char* function_name) {
    if (!prk_find_lua_script(state)->is_running_test) {
        luaL_error(state, "%s can only be called inside a test or hook", function_name);
    }
}

/** @brief Raises a usage error unless the top-level script is loading. */
static PrkLuaScript* prk_require_loading_lua_script(lua_State* state, const char* function_name) {
    PrkLuaScript* const script = prk_find_lua_script(state);
    if (script->loading_registry == NULL || script->is_running_test) {
        luaL_error(state, "%s can only be called while the script loads, not inside a test", function_name);
    }
    return script;
}

/** @brief prk.fail_test([message]). */
static int prk_lua_fail_test(lua_State* state) {
    prk_require_running_lua_test(state, "prk.fail_test");
    return prk_raise_lua_assertion(state, luaL_optstring(state, 1, "test failed"), 1);
}

/** @brief prk.skip_test([reason]). */
static int prk_lua_skip_test(lua_State* state) {
    prk_require_running_lua_test(state, "prk.skip_test");
    return prk_raise_lua_error_object(state, PRK_LUA_SKIP_TYPE, luaL_optstring(state, 1, "skipped"));
}

/* ------------------------------------------------------------------ registration */

/** @brief Options read from an add_test/describe options table. Strings stay owned by the table. */
typedef struct PrkLuaTestOptions {
    const char* tags[PRK_LUA_MAX_TAGS];
    size_t tag_count;
    uint64_t timeout_ms;
    const char* skip_reason;
} PrkLuaTestOptions;

/** @brief Reads options.tags (an array of strings) at stack index @p tags_index. */
static void prk_read_lua_tags(lua_State* state, int tags_index, const char* function_name, PrkLuaTestOptions* out) {
    luaL_argexpected(state, lua_istable(state, tags_index), tags_index, "table of tag strings");
    const lua_Integer count = (lua_Integer)lua_rawlen(state, tags_index);
    if (count > PRK_LUA_MAX_TAGS) {
        luaL_error(state, "%s: at most %d tags are allowed", function_name, PRK_LUA_MAX_TAGS);
    }
    for (lua_Integer index = 1; index <= count; ++index) {
        lua_rawgeti(state, tags_index, index);
        if (lua_type(state, -1) != LUA_TSTRING || lua_rawlen(state, -1) == 0) {
            luaL_error(state, "%s: tags must be non-empty strings", function_name);
        }
        out->tags[out->tag_count] = lua_tostring(state, -1); /* Kept alive by the tags table. */
        out->tag_count += 1;
        lua_pop(state, 1);
    }
}

/**
 * @brief Parses the options table at @p index (nil allowed). Unknown keys are errors so typos are caught.
 * @param allow_test_fields Non-zero for add_test (timeout_ms, skip); zero for describe (tags only).
 */
static void prk_read_lua_test_options(lua_State* state, int index, const char* function_name, int allow_test_fields,
                                      PrkLuaTestOptions* out) {
    memset(out, 0, sizeof *out);
    if (lua_isnoneornil(state, index)) {
        return;
    }
    luaL_checktype(state, index, LUA_TTABLE);
    lua_pushnil(state);
    while (lua_next(state, index) != 0) {
        const char* const key = lua_type(state, -2) == LUA_TSTRING ? lua_tostring(state, -2) : "";
        const int value_index = lua_gettop(state);
        if (strcmp(key, "tags") == 0) {
            prk_read_lua_tags(state, value_index, function_name, out);
        } else if (allow_test_fields && strcmp(key, "timeout_ms") == 0) {
            int is_integer = 0;
            const lua_Integer timeout = lua_tointegerx(state, value_index, &is_integer);
            if (!is_integer || timeout <= 0) {
                luaL_error(state, "%s: timeout_ms must be a positive integer", function_name);
            }
            out->timeout_ms = (uint64_t)timeout;
        } else if (allow_test_fields && strcmp(key, "skip") == 0) {
            if (lua_type(state, value_index) == LUA_TSTRING) {
                out->skip_reason = lua_tostring(state, value_index); /* Kept alive by the options table. */
            } else if (lua_toboolean(state, value_index)) {
                out->skip_reason = "skipped";
            }
        } else {
            luaL_error(state, "%s: unknown option '%s'", function_name, key);
        }
        lua_pop(state, 1);
    }
}

/** @brief Moves the function at the top of the stack into the registry and returns its reference. */
static PrkScriptFunction prk_reference_lua_function(lua_State* state, int function_index) {
    lua_pushvalue(state, function_index);
    return (PrkScriptFunction)luaL_ref(state, LUA_REGISTRYINDEX);
}

/** @brief prk.add_test(name, body [, options]). */
static int prk_lua_add_test(lua_State* state) {
    const char* const name = luaL_checkstring(state, 1);
    luaL_checktype(state, 2, LUA_TFUNCTION);
    PrkLuaScript* const script = prk_require_loading_lua_script(state, "prk.add_test");
    luaL_argcheck(state, name[0] != '\0', 1, "test name must not be empty");
    PrkLuaTestOptions options;
    prk_read_lua_test_options(state, 3, "prk.add_test", 1, &options);
    const PrkScriptFunction body = prk_reference_lua_function(state, 2);
    const PrkPlannedTestOptions planned = {
        name, body, options.tags, options.tag_count, options.timeout_ms, options.skip_reason};
    /* The plan owns body from here on; nothing below allocates Lua memory before the status check. */
    const PrkStatus status = prk_add_planned_test(script->plan, script->loading_registry, &planned, NULL);
    if (status != PRK_STATUS_OK) {
        script->registration_status = status;
        return luaL_error(state, "could not register test '%s': %s", name, prk_get_status_name(status));
    }
    return 0;
}

/** @brief prk.describe(name, define [, options]): runs @p define now with tests added to the suite. */
static int prk_lua_describe(lua_State* state) {
    const char* const name = luaL_checkstring(state, 1);
    luaL_checktype(state, 2, LUA_TFUNCTION);
    PrkLuaScript* const script = prk_require_loading_lua_script(state, "prk.describe");
    luaL_argcheck(state, name[0] != '\0' && strchr(name, '.') == NULL, 1, "suite name must be non-empty without '.'");
    PrkLuaTestOptions options;
    prk_read_lua_test_options(state, 3, "prk.describe", 0, &options);
    const PrkStatus status = prk_begin_test_suite(script->plan, name, options.tags, options.tag_count);
    if (status != PRK_STATUS_OK) {
        return luaL_error(state, "could not open suite '%s': %s (suites nest at most 31 deep)", name,
                          prk_get_status_name(status));
    }
    lua_pushvalue(state, 2);
    lua_call(state, 0, 0); /* An error here fails the whole load, so the open suite does not matter. */
    (void)prk_end_test_suite(script->plan);
    return 0;
}

/** @brief Shared body of the four hook registration functions; the kind is upvalue 1. */
static int prk_lua_add_suite_hook(lua_State* state) {
    const PrkSuiteHookKind kind = (PrkSuiteHookKind)lua_tointeger(state, lua_upvalueindex(1));
    const char* const function_name = lua_tostring(state, lua_upvalueindex(2));
    luaL_checktype(state, 1, LUA_TFUNCTION);
    PrkLuaScript* const script = prk_require_loading_lua_script(state, function_name);
    const PrkScriptFunction hook = prk_reference_lua_function(state, 1);
    const PrkStatus status = prk_add_suite_hook(script->plan, kind, hook);
    if (status != PRK_STATUS_OK) {
        return luaL_error(state, "%s: %s", function_name, prk_get_status_name(status));
    }
    return 0;
}

/* ------------------------------------------------------------------ host services */

/** @brief prk.read_clock_ms(): the host's monotonic clock in milliseconds. */
static int prk_lua_read_clock_ms(lua_State* state) {
    const PrkLuaScript* const script = prk_find_lua_script(state);
    if (script->host.read_monotonic_milliseconds == NULL) {
        return luaL_error(state, "prk.read_clock_ms: no clock is available in this host");
    }
    lua_pushinteger(state, (lua_Integer)script->host.read_monotonic_milliseconds(script->host.host_state));
    return 1;
}

/** @brief Raises the error for a failed file request. Does not return. */
static int prk_raise_lua_file_error(lua_State* state, const char* function_name, const char* path,
                                    PrkFileAccessStatus status) {
    if (status == PRK_FILE_ACCESS_DENIED) {
        return luaL_error(state, "%s: access denied for '%s' (only relative paths inside the working directory)",
                          function_name, path);
    }
    return luaL_error(state, "%s: '%s': %s", function_name, path, prk_get_file_access_status_name(status));
}

/** @brief prk.is_file_present(path) -> boolean. */
static int prk_lua_is_file_present(lua_State* state) {
    const char* const path = luaL_checkstring(state, 1);
    const PrkLuaScript* const script = prk_find_lua_script(state);
    if (script->host.find_file == NULL) {
        return luaL_error(state, "prk.is_file_present: file access is not available in this host");
    }
    const PrkFileAccessStatus status = script->host.find_file(script->host.host_state, path);
    if (status != PRK_FILE_ACCESS_OK && status != PRK_FILE_ACCESS_NOT_FOUND) {
        return prk_raise_lua_file_error(state, "prk.is_file_present", path, status);
    }
    lua_pushboolean(state, status == PRK_FILE_ACCESS_OK);
    return 1;
}

/** @brief Bytes collected from the host; a Lua userdata so a failed push cannot leak @p data. */
typedef struct PrkLuaFileBuffer {
    char* data;
    size_t length;
    size_t capacity;
    size_t limit;      /**< Most bytes the script may still allocate. */
    int is_over_limit; /**< The file is larger than @p limit or malloc failed. */
} PrkLuaFileBuffer;

/** @brief PrkReceiveFileBytes sink for prk.read_text_file. Plain C: never raises. */
static void prk_receive_lua_file_bytes(void* sink, const char* bytes, size_t length) {
    PrkLuaFileBuffer* const buffer = (PrkLuaFileBuffer*)sink;
    if (buffer->is_over_limit || length == 0) {
        return;
    }
    if (length > buffer->limit - buffer->length) {
        buffer->is_over_limit = 1;
        return;
    }
    if (buffer->length + length > buffer->capacity) {
        size_t capacity = buffer->capacity == 0 ? 4096 : buffer->capacity;
        while (capacity < buffer->length + length) {
            capacity = capacity > buffer->limit / 2 ? buffer->limit : capacity * 2;
        }
        char* const grown = (char*)realloc(buffer->data, capacity);
        if (grown == NULL) {
            buffer->is_over_limit = 1;
            return;
        }
        buffer->data = grown;
        buffer->capacity = capacity;
    }
    memcpy(buffer->data + buffer->length, bytes, length);
    buffer->length += length;
}

/** @brief __gc of PrkLuaFileBuffer. */
static int prk_free_lua_file_buffer(lua_State* state) {
    PrkLuaFileBuffer* const buffer = (PrkLuaFileBuffer*)luaL_checkudata(state, 1, PRK_LUA_FILE_BUFFER_TYPE);
    free(buffer->data);
    buffer->data = NULL;
    return 0;
}

/** @brief prk.read_text_file(path) -> string (the whole file, bytes unchanged). */
static int prk_lua_read_text_file(lua_State* state) {
    const char* const path = luaL_checkstring(state, 1);
    const PrkLuaScript* const script = prk_find_lua_script(state);
    if (script->host.read_file == NULL) {
        return luaL_error(state, "prk.read_text_file: file access is not available in this host");
    }
    PrkLuaFileBuffer* const buffer = (PrkLuaFileBuffer*)lua_newuserdatauv(state, sizeof(PrkLuaFileBuffer), 0);
    memset(buffer, 0, sizeof *buffer);
    luaL_setmetatable(state, PRK_LUA_FILE_BUFFER_TYPE);
    buffer->limit = script->memory_limit - script->memory_in_use;
    const PrkFileAccessStatus status =
        script->host.read_file(script->host.host_state, path, &prk_receive_lua_file_bytes, buffer);
    if (status != PRK_FILE_ACCESS_OK) {
        return prk_raise_lua_file_error(state, "prk.read_text_file", path, status);
    }
    if (buffer->is_over_limit) {
        return luaL_error(state, "prk.read_text_file: '%s' does not fit in the script memory limit", path);
    }
    lua_pushlstring(state, buffer->data != NULL ? buffer->data : "", buffer->length);
    free(buffer->data);
    buffer->data = NULL;
    return 1;
}

/* ------------------------------------------------------------------ assertion prelude */

/* Lua source of the assertions. Runs once per state with (prk, raise). Level 3 in fail() names the
   line of the test that called prk.expect_*. Keep in sync with docs/lua-tests.md. */
static const char* const prk_lua_assertion_prelude[] = {
    "local prk, raise = ...\n"
    "local type, tostring, pairs, rawget, pcall = type, tostring, pairs, rawget, pcall\n"
    "local format, find, abs = string.format, string.find, math.abs\n"
    "local function show(value)\n"
    "  if type(value) == 'string' then return format('%q', value) end\n"
    "  return tostring(value)\n"
    "end\n"
    "local function fail(message) raise(message, 3) end\n"
    "local function detail(message, default, text) return (message or default) .. ': ' .. text end\n"
    "local function require_number(value, function_name, what)\n"
    "  if type(value) ~= 'number' then error(function_name .. ': ' .. what .. ' must be a number', 3) end\n"
    "end\n"
    "function prk.expect_true(value, message)\n"
    "  if not value then fail(message or ('expected a true value, got ' .. show(value))) end\n"
    "end\n"
    "function prk.expect_false(value, message)\n"
    "  if value then fail(message or ('expected a false value, got ' .. show(value))) end\n"
    "end\n"
    "function prk.expect_equal(actual, expected, message)\n"
    "  if actual ~= expected then\n"
    "    fail(detail(message, 'values differ', 'expected ' .. show(expected) .. ', got ' .. show(actual)))\n"
    "  end\n"
    "end\n"
    "function prk.expect_not_equal(actual, unexpected, message)\n"
    "  if actual == unexpected then fail(detail(message, 'values are equal', 'both are ' .. show(actual))) end\n"
    "end\n"
    "function prk.expect_nil(value, message)\n"
    "  if value ~= nil then fail(detail(message, 'expected nil', 'got ' .. show(value))) end\n"
    "end\n",
    "function prk.expect_not_nil(value, message)\n"
    "  if value == nil then fail(message or 'expected a value, got nil') end\n"
    "end\n"
    "function prk.expect_near(actual, expected, tolerance, message)\n"
    "  require_number(actual, 'prk.expect_near', 'actual')\n"
    "  require_number(expected, 'prk.expect_near', 'expected')\n"
    "  require_number(tolerance, 'prk.expect_near', 'tolerance')\n"
    "  if not (abs(actual - expected) <= tolerance) then\n"
    "    fail(detail(message, 'values are not near', 'expected ' .. show(expected) .. ' +/- ' .. show(tolerance)\n"
    "      .. ', got ' .. show(actual)))\n"
    "  end\n"
    "end\n"
    "function prk.expect_less_than(actual, limit, message)\n"
    "  if not (actual < limit) then fail(detail(message, 'value too large', 'expected ' .. show(actual)\n"
    "    .. ' < ' .. show(limit))) end\n"
    "end\n"
    "function prk.expect_less_or_equal(actual, limit, message)\n"
    "  if not (actual <= limit) then fail(detail(message, 'value too large', 'expected ' .. show(actual)\n"
    "    .. ' <= ' .. show(limit))) end\n"
    "end\n"
    "function prk.expect_greater_than(actual, limit, message)\n"
    "  if not (actual > limit) then fail(detail(message, 'value too small', 'expected ' .. show(actual)\n"
    "    .. ' > ' .. show(limit))) end\n"
    "end\n"
    "function prk.expect_greater_or_equal(actual, limit, message)\n"
    "  if not (actual >= limit) then fail(detail(message, 'value too small', 'expected ' .. show(actual)\n"
    "    .. ' >= ' .. show(limit))) end\n"
    "end\n",
    "function prk.expect_contains(container, item, message)\n"
    "  local found = false\n"
    "  if type(container) == 'string' then\n"
    "    found = type(item) == 'string' and find(container, item, 1, true) ~= nil\n"
    "  elseif type(container) == 'table' then\n"
    "    for _, value in pairs(container) do if value == item then found = true break end end\n"
    "  else\n"
    "    error('prk.expect_contains: container must be a string or a table', 2)\n"
    "  end\n"
    "  if not found then\n"
    "    fail(detail(message, 'item not found', 'expected ' .. show(container) .. ' to contain ' .. show(item)))\n"
    "  end\n"
    "end\n"
    "function prk.expect_match(text, pattern, message)\n"
    "  if type(text) ~= 'string' or find(text, pattern) == nil then\n"
    "    fail(detail(message, 'no match', 'expected ' .. show(text) .. ' to match ' .. show(pattern)))\n"
    "  end\n"
    "end\n"
    "function prk.expect_type(value, type_name, message)\n"
    "  if type(value) ~= type_name then\n"
    "    fail(detail(message, 'wrong type', 'expected ' .. type_name .. ', got ' .. type(value)))\n"
    "  end\n"
    "end\n"
    "local function find_difference(actual, expected, path, seen)\n"
    "  if actual == expected then return nil end\n"
    "  if type(actual) ~= 'table' or type(expected) ~= 'table' then\n"
    "    return 'at ' .. path .. ': expected ' .. show(expected) .. ', got ' .. show(actual)\n"
    "  end\n"
    "  if seen[actual] == expected then return nil end\n"
    "  seen[actual] = expected\n"
    "  for key, value in pairs(expected) do\n"
    "    local difference = find_difference(rawget(actual, key), value, path .. '[' .. show(key) .. ']', seen)\n"
    "    if difference then return difference end\n"
    "  end\n"
    "  for key, value in pairs(actual) do\n"
    "    if rawget(expected, key) == nil then\n"
    "      return 'at ' .. path .. '[' .. show(key) .. ']: unexpected ' .. show(value)\n"
    "    end\n"
    "  end\n"
    "  return nil\n"
    "end\n",
    "function prk.expect_deep_equal(actual, expected, message)\n"
    "  local difference = find_difference(actual, expected, 'value', {})\n"
    "  if difference then fail(detail(message, 'values differ', difference)) end\n"
    "end\n"
    "function prk.expect_error(action, pattern, message)\n"
    "  if type(action) ~= 'function' then error('prk.expect_error: action must be a function', 2) end\n"
    "  local ok, problem = pcall(action)\n"
    "  if ok then fail(message or 'expected an error, but the function returned normally') end\n"
    "  local text = tostring(problem)\n"
    "  if pattern ~= nil and find(text, pattern) == nil then\n"
    "    fail(detail(message, 'wrong error', 'expected an error matching ' .. show(pattern) .. ', got ' .. "
    "show(text)))\n"
    "  end\n"
    "  return text\n"
    "end\n"
    "function prk.expect_file_present(path, message)\n"
    "  if not prk.is_file_present(path) then fail(message or ('expected file ' .. show(path) .. ' to exist')) end\n"
    "end\n"
    "function prk.expect_file_contains(path, text, message)\n"
    "  local content = prk.read_text_file(path)\n"
    "  if find(content, text, 1, true) == nil then\n"
    "    fail(detail(message, 'text not found', 'expected file ' .. show(path) .. ' to contain ' .. show(text)))\n"
    "  end\n"
    "end\n"
    "local function describe_process(result)\n"
    "  local text\n"
    "  if result.outcome == 'exited' then text = 'exit code ' .. tostring(result.exit_code)\n"
    "  elseif result.outcome == 'failed to start' then text = 'failed to start: ' .. tostring(result.error)\n"
    "  elseif result.outcome == 'timed out' then text = 'timed out after ' .. tostring(result.elapsed_ms) .. ' ms'\n"
    "  else text = tostring(result.outcome) .. ' ' .. tostring(result.signal) end\n"
    "  local stderr = result.stderr or ''\n"
    "  if #stderr > 0 then text = text .. '\\nstderr: ' .. stderr:sub(1, 2000) end\n"
    "  return text\n"
    "end\n"
    "function prk.expect_exit_code(result, expected, message)\n"
    "  if type(result) ~= 'table' or result.outcome == nil then\n"
    "    error('prk.expect_exit_code: result must come from prk.run_process', 2)\n"
    "  end\n"
    "  if result.outcome ~= 'exited' or result.exit_code ~= expected then\n"
    "    fail(detail(message, 'unexpected process result', 'expected exit code ' .. tostring(expected) .. ', got ' ..\n"
    "      describe_process(result)))\n"
    "  end\n"
    "end\n"
    "function prk.measure_elapsed_ms(action, ...)\n"
    "  local started = prk.read_clock_ms()\n"
    "  action(...)\n"
    "  return prk.read_clock_ms() - started\n"
    "end\n",
};

/** @brief State of prk_read_lua_prelude_piece: the next piece to hand to lua_load. */
typedef struct PrkLuaPreludeReader {
    size_t next_piece;
} PrkLuaPreludeReader;

/** @brief lua_Reader over prk_lua_assertion_prelude (split so no literal exceeds 4095 characters). */
static const char* prk_read_lua_prelude_piece(lua_State* state, void* reader_state, size_t* out_size) {
    (void)state;
    PrkLuaPreludeReader* const reader = (PrkLuaPreludeReader*)reader_state;
    const size_t piece_count = sizeof prk_lua_assertion_prelude / sizeof prk_lua_assertion_prelude[0];
    if (reader->next_piece >= piece_count) {
        *out_size = 0;
        return NULL;
    }
    const char* const piece = prk_lua_assertion_prelude[reader->next_piece];
    reader->next_piece += 1;
    *out_size = strlen(piece);
    return piece;
}

/** @brief Registers one hook function under @p name with its kind as an upvalue. */
static void prk_set_lua_hook_function(lua_State* state, const char* name, PrkSuiteHookKind kind) {
    lua_pushinteger(state, (lua_Integer)kind);
    lua_pushfstring(state, "prk.%s", name);
    lua_pushcclosure(state, &prk_lua_add_suite_hook, 2);
    lua_setfield(state, -2, name);
}

/** @brief Creates the metatable for one error-object type. */
static void prk_create_lua_error_type(lua_State* state, const char* type_name) {
    luaL_newmetatable(state, type_name);
    lua_pushcfunction(state, &prk_describe_lua_error_object);
    lua_setfield(state, -2, "__tostring");
    lua_pushstring(state, type_name);
    lua_setfield(state, -2, "__name");
    lua_pop(state, 1);
}

int prk_open_lua_test_api(lua_State* state) {
    static const luaL_Reg api_functions[] = {
        {"add_test", &prk_lua_add_test},
        {"describe", &prk_lua_describe},
        {"fail_test", &prk_lua_fail_test},
        {"skip_test", &prk_lua_skip_test},
        {"read_clock_ms", &prk_lua_read_clock_ms},
        {"is_file_present", &prk_lua_is_file_present},
        {"read_text_file", &prk_lua_read_text_file},
        {NULL, NULL},
    };
    prk_create_lua_error_type(state, PRK_LUA_ASSERTION_ERROR_TYPE);
    prk_create_lua_error_type(state, PRK_LUA_SKIP_TYPE);
    luaL_newmetatable(state, PRK_LUA_FILE_BUFFER_TYPE);
    lua_pushcfunction(state, &prk_free_lua_file_buffer);
    lua_setfield(state, -2, "__gc");
    lua_pop(state, 1);

    luaL_newlib(state, api_functions);
    prk_set_lua_hook_function(state, "before_all", PRK_HOOK_BEFORE_ALL);
    prk_set_lua_hook_function(state, "after_all", PRK_HOOK_AFTER_ALL);
    prk_set_lua_hook_function(state, "before_each", PRK_HOOK_BEFORE_EACH);
    prk_set_lua_hook_function(state, "after_each", PRK_HOOK_AFTER_EACH);
    lua_pushstring(state, prk_get_version_string());
    lua_setfield(state, -2, "version");
    lua_pushliteral(state, LUA_RELEASE);
    lua_setfield(state, -2, "lua_version");
    (void)prk_add_lua_process_api(state);

    PrkLuaPreludeReader reader = {0};
    if (lua_load(state, &prk_read_lua_prelude_piece, &reader, "=prk", "t") != LUA_OK) {
        return lua_error(state);
    }
    lua_pushvalue(state, -2);
    lua_pushcfunction(state, &prk_lua_raise_assertion);
    lua_call(state, 2, 0);
    lua_setglobal(state, "prk");
    return 0;
}
