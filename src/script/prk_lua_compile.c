/**
 * @file prk_lua_compile.c
 * @brief prk.compile (#14, C/C++ driver) and prk.assemble (#16, assembler pass) for Lua test scripts, over the
 *        #13 spawn layer.
 *
 *   local result = prk.compile{sources = {"hello.c"}, output = "build/hello", language = "c",
 *                              kind = "executable" | "object" | "static_library" | "shared_library",
 *                              compiler = "auto", program = nil, archiver = nil, msvc_arch = "x64",
 *                              std = "c17", includes = {"include"}, defines = {"NDEBUG"}, flags = {"-O2"},
 *                              libraries = {"m"}, library_dirs = {"lib"}, werror = true, cwd = nil,
 *                              timeout_ms = 120000}
 *
 * The result has every prk.run_process field (ok, outcome, exit_code, stdout, stderr, elapsed_ms, error)
 * plus compiler, program, command, output, kind, msvc_discovered, errors, warnings, and diagnostics (a sequence of
 * {severity, file, line, column, code, message}). A missing compiler is a result, not an error.
 *
 *   local object = prk.assemble{source = "add.asm", output = "add.obj", assembler = "auto" | "masm" | "gas" | "nasm",
 *                               program = nil, includes = {...}, defines = {...}, flags = {...}, werror = true,
 *                               format = "win64", msvc_arch = "x64", cwd = nil, timeout_ms = 60000}
 *
 * prk.assemble (#16) returns the same fields with `assembler` instead of `compiler`; link the object with
 * prk.compile.
 */
#include <string.h>

#include "lauxlib.h"
#include "prk_lua_internal.h"
#include "proofrunkit/prk_asm_driver.h"
#include "proofrunkit/prk_c_driver.h"

/** @brief Registry name of the userdata that owns a PrkCompileResult until Lua has copied it. */
#define PRK_LUA_COMPILE_RESULT_TYPE "prk.CompileResult"

/** @brief __gc of the result holder. */
static int prk_free_lua_compile_result(lua_State* state) {
    PrkCompileResult* const result = (PrkCompileResult*)luaL_checkudata(state, 1, PRK_LUA_COMPILE_RESULT_TYPE);
    prk_release_compile_result(result);
    return 0;
}

/** @brief Name of the running API function ("prk.compile" or "prk.assemble"), its closure's upvalue. */
static const char* prk_get_lua_api_name(lua_State* state) {
    const char* const name = lua_tostring(state, lua_upvalueindex(1));
    return name != NULL ? name : "prk";
}

/** @brief Rejects option keys not in @p known (NULL-terminated). */
static void prk_check_lua_option_keys(lua_State* state, int options, const char* const* known) {
    lua_pushnil(state);
    while (lua_next(state, options) != 0) {
        const char* const key = lua_type(state, -2) == LUA_TSTRING ? lua_tostring(state, -2) : NULL;
        int is_known = 0;
        for (size_t index = 0; key != NULL && known[index] != NULL; ++index) {
            is_known |= strcmp(key, known[index]) == 0;
        }
        if (!is_known) {
            luaL_error(state, "%s: unknown option '%s'", prk_get_lua_api_name(state),
                       key != NULL ? key : "(non-string key)");
        }
        lua_pop(state, 1);
    }
}

/** @brief Reads an optional string option (kept alive by the options table). */
static const char* prk_read_lua_compile_string(lua_State* state, int options, const char* key) {
    const int type = lua_getfield(state, options, key);
    const char* const text = type == LUA_TSTRING ? lua_tostring(state, -1) : NULL;
    if (type != LUA_TSTRING && type != LUA_TNIL) {
        luaL_error(state, "%s: %s must be a string", prk_get_lua_api_name(state), key);
    }
    lua_pop(state, 1);
    return text;
}

/**
 * @brief Reads a string or a sequence of strings into a pointer array; leaves one holder value on the stack.
 * @return The array (NULL when absent).
 */
static const char* const* prk_read_lua_string_list(lua_State* state, int options, const char* key, size_t* out_count) {
    const int type = lua_getfield(state, options, key);
    *out_count = 0;
    if (type == LUA_TNIL) {
        return NULL; /* The nil stays as the holder. */
    }
    if (type == LUA_TSTRING) {
        const char** const pointers = (const char**)lua_newuserdatauv(state, sizeof *pointers, 0);
        pointers[0] = lua_tostring(state, -2);
        lua_remove(state, -2); /* The options table keeps the string alive. */
        *out_count = 1;
        return pointers;
    }
    if (type != LUA_TTABLE) {
        luaL_error(state, "%s: %s must be a string or a sequence of strings", prk_get_lua_api_name(state), key);
    }
    const size_t count = (size_t)luaL_len(state, -1);
    const char** const pointers = (const char**)lua_newuserdatauv(state, (count + 1) * sizeof *pointers, 0);
    for (size_t index = 0; index < count; ++index) {
        if (lua_geti(state, -2, (lua_Integer)index + 1) != LUA_TSTRING) {
            luaL_error(state, "%s: %s[%d] must be a string", prk_get_lua_api_name(state), key, (int)index + 1);
        }
        pointers[index] = lua_tostring(state, -1); /* Kept alive by the list table in the options table. */
        lua_pop(state, 1);
    }
    lua_remove(state, -2);
    *out_count = count;
    return pointers;
}

/** @brief Maps a compiler or language name. @return its index in @p names, or raises. */
static int prk_read_lua_choice(lua_State* state, int options, const char* key, const char* const* names, int fallback) {
    const char* const text = prk_read_lua_compile_string(state, options, key);
    if (text == NULL) {
        return fallback;
    }
    for (int index = 0; names[index] != NULL; ++index) {
        if (strcmp(text, names[index]) == 0) {
            return index;
        }
    }
    return luaL_error(state, "%s: unknown %s '%s'", prk_get_lua_api_name(state), key, text);
}

/** @brief Reads the list options; leaves one holder per list on the stack. */
static void prk_read_lua_compile_lists(lua_State* state, PrkCompileRequest* out_request) {
    out_request->sources = prk_read_lua_string_list(state, 1, "sources", &out_request->source_count);
    luaL_argcheck(state, out_request->source_count > 0, 1, "prk.compile: sources must name at least one file");
    luaL_argcheck(state, out_request->kind != PRK_OUTPUT_OBJECT || out_request->source_count == 1, 1,
                  "prk.compile: kind = \"object\" compiles exactly one source");
    out_request->include_directories =
        prk_read_lua_string_list(state, 1, "includes", &out_request->include_directory_count);
    out_request->defines = prk_read_lua_string_list(state, 1, "defines", &out_request->define_count);
    out_request->extra_flags = prk_read_lua_string_list(state, 1, "flags", &out_request->extra_flag_count);
    out_request->libraries = prk_read_lua_string_list(state, 1, "libraries", &out_request->library_count);
    out_request->library_directories =
        prk_read_lua_string_list(state, 1, "library_dirs", &out_request->library_directory_count);
}

/** @brief Reads timeout_ms and caps it by the running test's deadline. */
static uint64_t prk_read_lua_compile_timeout(lua_State* state, const PrkLuaScript* script) {
    lua_Integer requested = 0;
    if (lua_getfield(state, 1, "timeout_ms") != LUA_TNIL) {
        int is_integer = 0;
        requested = lua_tointegerx(state, -1, &is_integer);
        if (!is_integer || requested <= 0 || requested > (lua_Integer)PRK_PROCESS_MAX_TIMEOUT_MS) {
            luaL_error(state, "%s: timeout_ms must be a positive integer up to one day", prk_get_lua_api_name(state));
        }
    }
    lua_pop(state, 1);
    const uint64_t now = script->host.read_monotonic_milliseconds != NULL
                             ? script->host.read_monotonic_milliseconds(script->host.host_state)
                             : 0;
    return prk_cap_script_process_timeout_ms((uint64_t)requested, script->deadline_ms, now);
}

/** @brief Reads every option into @p out_request; leaves the list holders on the stack. */
static void prk_read_lua_compile_request(lua_State* state, const PrkLuaScript* script, PrkCompileRequest* out_request) {
    static const char* const families[] = {"auto", "gcc", "clang", "msvc", NULL};
    static const char* const languages[] = {"c", "c++", NULL};
    static const char* const kinds[] = {"executable", "object", "static_library", "shared_library", NULL};
    memset(out_request, 0, sizeof *out_request);
    out_request->family = (PrkCompilerFamily)prk_read_lua_choice(state, 1, "compiler", families, 0);
    out_request->language = (PrkSourceLanguage)prk_read_lua_choice(state, 1, "language", languages, 0);
    out_request->kind = (PrkOutputKind)prk_read_lua_choice(state, 1, "kind", kinds, 0);
    out_request->compiler_program = prk_read_lua_compile_string(state, 1, "program");
    out_request->archiver_program = prk_read_lua_compile_string(state, 1, "archiver");
    luaL_argcheck(state,
                  (out_request->compiler_program == NULL && out_request->archiver_program == NULL) ||
                      out_request->family != PRK_COMPILER_AUTO,
                  1, "prk.compile: program and archiver need compiler = \"gcc\", \"clang\", or \"msvc\"");
    out_request->msvc_architecture = prk_read_lua_compile_string(state, 1, "msvc_arch");
    luaL_argcheck(
        state,
        out_request->msvc_architecture == NULL || prk_is_msvc_architecture_allowed(out_request->msvc_architecture), 1,
        "prk.compile: msvc_arch must be a vcvarsall architecture such as \"x64\" or \"x86\"");
    out_request->output_path = prk_read_lua_compile_string(state, 1, "output");
    luaL_argcheck(state, out_request->output_path != NULL && out_request->output_path[0] != '\0', 1,
                  "prk.compile: output must be a non-empty string");
    out_request->standard = prk_read_lua_compile_string(state, 1, "std");
    out_request->working_directory = prk_read_lua_compile_string(state, 1, "cwd");
    lua_getfield(state, 1, "werror");
    out_request->is_warnings_as_errors = lua_toboolean(state, -1);
    lua_pop(state, 1);
    prk_read_lua_compile_lists(state, out_request);
    out_request->timeout_ms = prk_read_lua_compile_timeout(state, script);
    out_request->msvc_toolchain = script->host.msvc_toolchain;
}

/** @brief Appends the diagnostics sequence and the compile fields to the result table on top. */
static void prk_add_lua_compile_fields(lua_State* state, const PrkCompileResult* result) {
    static const char* const severities[] = {"error", "warning", "note"};
    lua_pushstring(state, prk_get_compiler_family_name(result->family));
    lua_setfield(state, -2, "compiler");
    lua_pushstring(state, result->compiler_program);
    lua_setfield(state, -2, "program");
    lua_pushstring(state, result->command_line != NULL ? result->command_line : "");
    lua_setfield(state, -2, "command");
    lua_pushstring(state, result->output_path);
    lua_setfield(state, -2, "output");
    lua_pushstring(state, prk_get_output_kind_name(result->kind));
    lua_setfield(state, -2, "kind");
    lua_pushboolean(state, result->is_msvc_discovered);
    lua_setfield(state, -2, "msvc_discovered");
    lua_pushinteger(state, (lua_Integer)result->error_count);
    lua_setfield(state, -2, "errors");
    lua_pushinteger(state, (lua_Integer)result->warning_count);
    lua_setfield(state, -2, "warnings");
    lua_createtable(state, (int)result->diagnostic_count, 0);
    for (size_t index = 0; index < result->diagnostic_count; ++index) {
        const PrkCompilerDiagnostic* const diagnostic = &result->diagnostics[index];
        lua_createtable(state, 0, 6);
        lua_pushstring(state, severities[diagnostic->severity]);
        lua_setfield(state, -2, "severity");
        lua_pushstring(state, diagnostic->file);
        lua_setfield(state, -2, "file");
        lua_pushinteger(state, (lua_Integer)diagnostic->line);
        lua_setfield(state, -2, "line");
        lua_pushinteger(state, (lua_Integer)diagnostic->column);
        lua_setfield(state, -2, "column");
        lua_pushstring(state, diagnostic->code);
        lua_setfield(state, -2, "code");
        lua_pushstring(state, diagnostic->message);
        lua_setfield(state, -2, "message");
        lua_rawseti(state, -2, (lua_Integer)index + 1);
    }
    lua_setfield(state, -2, "diagnostics");
}

/** @brief prk.compile(options) -> result table. */
static int prk_lua_compile(lua_State* state) {
    const PrkLuaScript* const script = prk_find_lua_script(state);
    luaL_checktype(state, 1, LUA_TTABLE);
    if (script->host.process_runner.spawn_process == NULL) {
        return luaL_error(state, "prk.compile: process spawn is not available in this host");
    }
    static const char* const known[] = {"sources",   "output",       "language", "compiler",  "program",    "std",
                                        "includes",  "defines",      "flags",    "werror",    "cwd",        "kind",
                                        "libraries", "library_dirs", "archiver", "msvc_arch", "timeout_ms", NULL};
    prk_check_lua_option_keys(state, 1, known);
    PrkCompileRequest request;
    prk_read_lua_compile_request(state, script, &request);
    PrkCompileResult* const result = (PrkCompileResult*)lua_newuserdatauv(state, sizeof *result, 0);
    memset(result, 0, sizeof *result);
    luaL_setmetatable(state, PRK_LUA_COMPILE_RESULT_TYPE);
    const PrkStatus status = prk_compile_program(&script->host.process_runner, &request, result);
    if (status != PRK_STATUS_OK) {
        return luaL_error(state, "prk.compile: %s", prk_get_status_name(status));
    }
    prk_push_lua_process_result(state, &result->process);
    prk_add_lua_compile_fields(state, result);
    prk_release_compile_result(result);
    return 1;
}

/** @brief Reads every prk.assemble option into @p out_request; leaves the list holders on the stack. */
static void prk_read_lua_assemble_request(lua_State* state, const PrkLuaScript* script,
                                          PrkAssembleRequest* out_request) {
    static const char* const families[] = {"auto", "masm", "gas", "nasm", NULL};
    memset(out_request, 0, sizeof *out_request);
    out_request->family = (PrkAssemblerFamily)prk_read_lua_choice(state, 1, "assembler", families, 0);
    out_request->assembler_program = prk_read_lua_compile_string(state, 1, "program");
    out_request->source = prk_read_lua_compile_string(state, 1, "source");
    out_request->output_path = prk_read_lua_compile_string(state, 1, "output");
    out_request->object_format = prk_read_lua_compile_string(state, 1, "format");
    out_request->msvc_architecture = prk_read_lua_compile_string(state, 1, "msvc_arch");
    out_request->working_directory = prk_read_lua_compile_string(state, 1, "cwd");
    luaL_argcheck(state, out_request->source != NULL && out_request->output_path != NULL, 1,
                  "prk.assemble: source and output are required");
    luaL_argcheck(state, out_request->assembler_program == NULL || out_request->family != PRK_ASSEMBLER_AUTO, 1,
                  "prk.assemble: program needs assembler = \"masm\", \"gas\", or \"nasm\"");
    luaL_argcheck(state, prk_choose_assembler_family(out_request->family, out_request->source) != PRK_ASSEMBLER_AUTO, 1,
                  "prk.assemble: unknown source extension; name the assembler");
    luaL_argcheck(
        state,
        out_request->msvc_architecture == NULL || prk_is_msvc_architecture_allowed(out_request->msvc_architecture), 1,
        "prk.assemble: msvc_arch must be a vcvarsall architecture such as \"x64\" or \"x86\"");
    lua_getfield(state, 1, "werror");
    out_request->is_warnings_as_errors = lua_toboolean(state, -1);
    lua_pop(state, 1);
    out_request->include_directories =
        prk_read_lua_string_list(state, 1, "includes", &out_request->include_directory_count);
    out_request->defines = prk_read_lua_string_list(state, 1, "defines", &out_request->define_count);
    out_request->extra_flags = prk_read_lua_string_list(state, 1, "flags", &out_request->extra_flag_count);
    out_request->timeout_ms = prk_read_lua_compile_timeout(state, script);
    out_request->msvc_toolchain = script->host.msvc_toolchain;
}

/** @brief prk.assemble(options) -> result table (the prk.compile fields, with assembler instead of compiler). */
static int prk_lua_assemble(lua_State* state) {
    static const char* const known[] = {"source", "output", "assembler", "program", "includes",   "defines", "flags",
                                        "werror", "format", "msvc_arch", "cwd",     "timeout_ms", NULL};
    const PrkLuaScript* const script = prk_find_lua_script(state);
    luaL_checktype(state, 1, LUA_TTABLE);
    if (script->host.process_runner.spawn_process == NULL) {
        return luaL_error(state, "prk.assemble: process spawn is not available in this host");
    }
    prk_check_lua_option_keys(state, 1, known);
    PrkAssembleRequest request;
    prk_read_lua_assemble_request(state, script, &request);
    PrkCompileResult* const result = (PrkCompileResult*)lua_newuserdatauv(state, sizeof *result, 0);
    memset(result, 0, sizeof *result);
    luaL_setmetatable(state, PRK_LUA_COMPILE_RESULT_TYPE);
    const PrkStatus status = prk_assemble_source(&script->host.process_runner, &request, result);
    if (status != PRK_STATUS_OK) {
        return luaL_error(state, "prk.assemble: %s", prk_get_status_name(status));
    }
    prk_push_lua_process_result(state, &result->process);
    prk_add_lua_compile_fields(state, result);
    lua_pushnil(state);
    lua_setfield(state, -2, "compiler");
    lua_pushstring(state, prk_get_assembler_family_name(prk_choose_assembler_family(request.family, request.source)));
    lua_setfield(state, -2, "assembler");
    prk_release_compile_result(result);
    return 1;
}

int prk_add_lua_compile_api(lua_State* state) {
    luaL_newmetatable(state, PRK_LUA_COMPILE_RESULT_TYPE);
    lua_pushcfunction(state, &prk_free_lua_compile_result);
    lua_setfield(state, -2, "__gc");
    lua_pop(state, 1);
    lua_pushstring(state, "prk.compile");
    lua_pushcclosure(state, &prk_lua_compile, 1);
    lua_setfield(state, -2, "compile");
    lua_pushstring(state, "prk.assemble");
    lua_pushcclosure(state, &prk_lua_assemble, 1);
    lua_setfield(state, -2, "assemble");
    return 0;
}
