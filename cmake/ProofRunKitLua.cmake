# Builds the vendored Lua 5.4.9 as a static library linked into the single proofrunkit executable (#9).
#
# Only the VM core and the safe standard libraries are compiled. linit.c, liolib.c, loslib.c,
# loadlib.c, and ldblib.c (all libraries, io, os, package/require, debug) and the stand-alone
# lua.c / luac.c programs are deliberately left out, so their code is not in the binary at all.
# See third_party/lua-5.4.9/PROOFRUNKIT.md.

set(PRK_LUA_VERSION "5.4.9")
set(PRK_LUA_SOURCE_DIR "${PROJECT_SOURCE_DIR}/third_party/lua-${PRK_LUA_VERSION}/src")

set(_prk_lua_core_sources
    lapi.c lcode.c lctype.c ldebug.c ldo.c ldump.c lfunc.c lgc.c llex.c lmem.c lobject.c lopcodes.c
    lparser.c lstate.c lstring.c ltable.c ltm.c lundump.c lvm.c lzio.c)
set(_prk_lua_safe_library_sources
    lauxlib.c lbaselib.c lcorolib.c lmathlib.c lstrlib.c ltablib.c lutf8lib.c)
list(TRANSFORM _prk_lua_core_sources PREPEND "${PRK_LUA_SOURCE_DIR}/")
list(TRANSFORM _prk_lua_safe_library_sources PREPEND "${PRK_LUA_SOURCE_DIR}/")

add_library(prk_lua STATIC ${_prk_lua_core_sources} ${_prk_lua_safe_library_sources})
# Lua's headers are not public ProofRunKit API; only the engine sources include them.
target_include_directories(prk_lua PUBLIC "$<BUILD_INTERFACE:${PRK_LUA_SOURCE_DIR}>")
set_target_properties(prk_lua PROPERTIES C_STANDARD 17 C_EXTENSIONS OFF POSITION_INDEPENDENT_CODE ON)
if(MSVC)
    # Vendored code is compiled unmodified with the toolchain's default warning level and never
    # with warnings-as-errors. lauxlib.c uses fopen for luaL_loadfilex, which the sandbox does not expose.
    target_compile_definitions(prk_lua PRIVATE _CRT_SECURE_NO_WARNINGS)
    target_compile_options(prk_lua PRIVATE /W1 $<$<BOOL:${PRK_REPRODUCIBLE_BUILD}>:/Brepro>)
else()
    target_compile_options(prk_lua PRIVATE -w)
endif()
