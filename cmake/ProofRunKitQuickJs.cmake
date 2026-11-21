# Builds the vendored QuickJS-ng 0.18.0 as a static library linked into the single proofrunkit
# executable (#11).
#
# Only the engine core is compiled: quickjs.c, libregexp.c, libunicode.c, and dtoa.c. quickjs-libc.c
# (the `std` and `os` modules: files, processes, environment, timers) and the qjs / qjsc programs are
# not vendored or compiled, so their code is not in the binary at all.
# See third_party/quickjs-ng-0.18.0/PROOFRUNKIT.md.

set(PRK_QUICKJS_VERSION "0.18.0")
set(PRK_QUICKJS_SOURCE_DIR "${PROJECT_SOURCE_DIR}/third_party/quickjs-ng-${PRK_QUICKJS_VERSION}")

set(_prk_quickjs_sources quickjs.c libregexp.c libunicode.c dtoa.c)
list(TRANSFORM _prk_quickjs_sources PREPEND "${PRK_QUICKJS_SOURCE_DIR}/")

add_library(prk_quickjs STATIC ${_prk_quickjs_sources})
# QuickJS headers are not public ProofRunKit API; only the engine sources include them.
target_include_directories(prk_quickjs PUBLIC "$<BUILD_INTERFACE:${PRK_QUICKJS_SOURCE_DIR}>")
# Upstream builds as C11 with compiler extensions (gnu11 on GCC/Clang); MSVC gets /std:c11.
set_target_properties(prk_quickjs PROPERTIES C_STANDARD 11 C_STANDARD_REQUIRED ON C_EXTENSIONS ON
                                             POSITION_INDEPENDENT_CODE ON)
# Same definitions as upstream's CMakeLists.txt for a static, libc-less library.
target_compile_definitions(prk_quickjs PRIVATE QUICKJS_NG_BUILD _GNU_SOURCE)
if(WIN32)
    target_compile_definitions(prk_quickjs PRIVATE WIN32_LEAN_AND_MEAN _WIN32_WINNT=0x0601)
else()
    target_link_libraries(prk_quickjs PUBLIC m)
endif()

if(MSVC)
    # Vendored code is compiled unmodified at a low warning level and never with warnings-as-errors.
    # MSVC needs /experimental:c11atomics for <stdatomic.h> (used by quickjs-c-atomics.h), as upstream does.
    include(CheckCCompilerFlag)
    check_c_compiler_flag(/experimental:c11atomics PRK_MSVC_HAS_C11_ATOMICS_FLAG)
    target_compile_definitions(prk_quickjs PRIVATE _CRT_SECURE_NO_WARNINGS _CRT_NONSTDC_NO_DEPRECATE)
    target_compile_options(prk_quickjs PRIVATE /W1 $<$<BOOL:${PRK_MSVC_HAS_C11_ATOMICS_FLAG}>:/experimental:c11atomics>
                                               $<$<BOOL:${PRK_REPRODUCIBLE_BUILD}>:/Brepro>)
else()
    # Upstream compiles with -funsigned-char on GCC and Clang.
    target_compile_options(prk_quickjs PRIVATE -w -funsigned-char)
endif()
