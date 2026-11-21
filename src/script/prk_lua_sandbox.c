/**
 * @file prk_lua_sandbox.c
 * @brief Sandbox for the embedded Lua 5.4 engine (#9): memory-limited allocator, instruction budget,
 *        safe library set, print redirection, text-only load, and error formatting.
 *
 * Must not open io, os, package, or debug, and must not expose dofile, loadfile, or string.dump:
 * those give a test script ambient file, process, or native-code access, or let it remove the
 * instruction hook. The excluded libraries are not even compiled (cmake/ProofRunKitLua.cmake).
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "lauxlib.h"
#include "lualib.h"
#include "prk_lua_internal.h"

/* Address-only marker; never written, so it is not mutable state. */
static const char prk_lua_stop_marker_key = 0;

void* prk_get_lua_stop_marker(void) {
    return (void*)&prk_lua_stop_marker_key;
}

PrkLuaScript* prk_find_lua_script(lua_State* state) {
    void* script_state = NULL;
    (void)lua_getallocf(state, &script_state);
    return (PrkLuaScript*)script_state;
}

void* prk_allocate_lua_memory(void* script_state, void* block, size_t old_size, size_t new_size) {
    PrkLuaScript* const script = (PrkLuaScript*)script_state;
    /* When block is NULL, old_size encodes the object type rather than a size. */
    const size_t current_size = block == NULL ? 0 : old_size;
    if (new_size == 0) {
        free(block);
        script->memory_in_use -= current_size;
        return NULL;
    }
    if (new_size > current_size && new_size - current_size > script->memory_limit - script->memory_in_use) {
        return NULL; /* Lua turns this into a "not enough memory" error (LUA_ERRMEM). */
    }
    void* const resized = realloc(block, new_size);
    if (resized != NULL) {
        script->memory_in_use = script->memory_in_use - current_size + new_size;
    }
    return resized;
}

/** @brief Count hook: charges the instructions since the last call and stops the script past its budget. */
static void prk_charge_lua_instructions(lua_State* state, lua_Debug* debug_record) {
    (void)debug_record;
    PrkLuaScript* const script = prk_find_lua_script(state);
    script->instructions_used += (uint64_t)lua_gethookcount(state);
    if (script->instructions_used <= script->instruction_limit) {
        return;
    }
    /* Fire on every instruction from now on, so a script that catches this error with pcall is
       stopped again at its very next instruction instead of 1000 instructions later. */
    lua_sethook(state, &prk_charge_lua_instructions, LUA_MASKCOUNT, 1);
    luaL_error(state, "instruction limit of %I instructions exceeded", (lua_Integer)script->instruction_limit);
}

void prk_reset_lua_instruction_budget(PrkLuaScript* script) {
    script->instructions_used = 0;
    lua_sethook(script->state, &prk_charge_lua_instructions, LUA_MASKCOUNT, PRK_LUA_HOOK_INTERVAL);
}

/** @brief print(...) replacement: writes tab-separated values and a newline to the output callback. */
static int prk_print_to_lua_output(lua_State* state) {
    const PrkLuaScript* const script = prk_find_lua_script(state);
    const int argument_count = lua_gettop(state);
    for (int index = 1; index <= argument_count; ++index) {
        size_t length = 0;
        const char* const text = luaL_tolstring(state, index, &length);
        if (script->output.write_text != NULL) {
            if (index > 1) {
                script->output.write_text(script->output.output_state, "\t", 1);
            }
            script->output.write_text(script->output.output_state, text, length);
        }
        lua_pop(state, 1);
    }
    if (script->output.write_text != NULL) {
        script->output.write_text(script->output.output_state, "\n", 1);
    }
    return 0;
}

/** @brief load(chunk, name, mode, env) replacement that always uses mode "t" (no bytecode). */
static int prk_load_text_chunk_only(lua_State* state) {
    if (lua_gettop(state) < 3) {
        lua_settop(state, 3);
    }
    lua_pushliteral(state, "t");
    lua_replace(state, 3);
    const int argument_count = lua_gettop(state);
    lua_pushvalue(state, lua_upvalueindex(1));
    lua_insert(state, 1);
    lua_call(state, argument_count, LUA_MULTRET);
    return lua_gettop(state);
}

/** @brief Opens one standard library under @p name and drops it from the stack. */
static void prk_open_lua_library(lua_State* state, const char* name, lua_CFunction open_library) {
    luaL_requiref(state, name, open_library, 1);
    lua_pop(state, 1);
}

int prk_open_lua_sandbox_libraries(lua_State* state) {
    prk_open_lua_library(state, LUA_GNAME, &luaopen_base);
    prk_open_lua_library(state, LUA_COLIBNAME, &luaopen_coroutine);
    prk_open_lua_library(state, LUA_TABLIBNAME, &luaopen_table);
    prk_open_lua_library(state, LUA_STRLIBNAME, &luaopen_string);
    prk_open_lua_library(state, LUA_MATHLIBNAME, &luaopen_math);
    prk_open_lua_library(state, LUA_UTF8LIBNAME, &luaopen_utf8);

    lua_pushnil(state);
    lua_setglobal(state, "dofile");
    lua_pushnil(state);
    lua_setglobal(state, "loadfile");
    lua_pushcfunction(state, &prk_print_to_lua_output);
    lua_setglobal(state, "print");
    lua_getglobal(state, "load");
    lua_pushcclosure(state, &prk_load_text_chunk_only, 1);
    lua_setglobal(state, "load");

    lua_getglobal(state, LUA_STRLIBNAME);
    lua_pushnil(state);
    lua_setfield(state, -2, "dump");
    lua_pop(state, 1);
    return 0;
}

int prk_add_lua_error_traceback(lua_State* state) {
    if (lua_touserdata(state, 1) == prk_get_lua_stop_marker()) {
        return 1;
    }
    const char* message = lua_tostring(state, 1);
    if (message == NULL) {
        if (luaL_callmeta(state, 1, "__tostring") && lua_type(state, -1) == LUA_TSTRING) {
            return 1;
        }
        message = lua_pushfstring(state, "(error object is a %s value)", luaL_typename(state, 1));
    }
    luaL_traceback(state, state, message, 1);
    return 1;
}

/** @brief Copies @p text into @p out_text, truncating and always terminating. */
static void prk_copy_lua_text(const char* text, char* out_text, size_t capacity) {
    if (out_text == NULL || capacity == 0) {
        return;
    }
    const size_t length = strlen(text);
    const size_t copied = length < capacity - 1 ? length : capacity - 1;
    memcpy(out_text, text, copied);
    out_text[copied] = '\0';
}

void prk_describe_lua_error(const PrkLuaScript* script, int pcall_status, char* out_text, size_t capacity) {
    if (pcall_status == LUA_ERRMEM) {
        char text[128];
        (void)snprintf(text, sizeof text, "memory limit of %zu bytes exceeded", script->memory_limit);
        prk_copy_lua_text(text, out_text, capacity);
        return;
    }
    const char* const message = lua_tostring(script->state, -1);
    prk_copy_lua_text(message != NULL ? message : "unknown Lua error", out_text, capacity);
}
