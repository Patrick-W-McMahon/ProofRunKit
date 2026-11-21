# Selects the prk_read_cycle_counter implementation (#8, #39).
#
# Sets in the caller's scope:
#   PRK_CYCLE_COUNTER_SOURCES        sources that define prk_read_cycle_counter
#   PRK_CYCLE_COUNTER_IMPLEMENTATION name reported by prk_get_cycle_counter_implementation_name()
#
# Every Assembly routine has a mandatory C fallback (prk_read_cycle_counter_fallback), which is
# always compiled so the self-test can compare the two.

set(_prk_asm_dir "${CMAKE_CURRENT_LIST_DIR}/../src/asm")
set(_prk_c_only "${CMAKE_CURRENT_LIST_DIR}/../src/timing/prk_cycle_counter_c_only.c")
set(_prk_processor "${CMAKE_SYSTEM_PROCESSOR}")
if(MSVC AND CMAKE_C_COMPILER_ARCHITECTURE_ID)
    set(_prk_processor "${CMAKE_C_COMPILER_ARCHITECTURE_ID}")
endif()
string(TOLOWER "${_prk_processor}" _prk_processor)

if(PRK_FORCE_C_FALLBACK)
    set(PRK_CYCLE_COUNTER_SOURCES "${_prk_c_only}")
    set(PRK_CYCLE_COUNTER_IMPLEMENTATION "c-fallback")
elseif(MSVC AND _prk_processor MATCHES "^(x64|amd64|x86_64)$")
    enable_language(ASM_MASM)
    set(PRK_CYCLE_COUNTER_SOURCES "${_prk_asm_dir}/x86_64_windows.asm")
    set(PRK_CYCLE_COUNTER_IMPLEMENTATION "asm-masm-x64")
elseif(MSVC AND (CMAKE_GENERATOR_PLATFORM STREQUAL "Win32" OR (CMAKE_SIZEOF_VOID_P EQUAL 4 AND _prk_processor MATCHES "^(x86|i386|i686)$")))
    # 32-bit MSVC (Win32): ml.exe, cdecl. /safeseh is required so the object can be
    # linked into an image that uses /SAFESEH (MSVC's default for 32-bit).
    enable_language(ASM_MASM)
    set(PRK_CYCLE_COUNTER_SOURCES "${_prk_asm_dir}/x86_windows.asm")
    set(PRK_CYCLE_COUNTER_IMPLEMENTATION "asm-masm-x86")
    set_source_files_properties("${_prk_asm_dir}/x86_windows.asm" PROPERTIES COMPILE_FLAGS "/safeseh")
elseif(NOT WIN32 AND _prk_processor MATCHES "^(x86_64|amd64)$" AND CMAKE_SIZEOF_VOID_P EQUAL 8)
    enable_language(ASM)
    set(PRK_CYCLE_COUNTER_SOURCES "${_prk_asm_dir}/x86_64_sysv.S")
    set(PRK_CYCLE_COUNTER_IMPLEMENTATION "asm-gas-x86_64-sysv")
elseif(NOT WIN32 AND _prk_processor MATCHES "^(aarch64|arm64)$")
    enable_language(ASM)
    set(PRK_CYCLE_COUNTER_SOURCES "${_prk_asm_dir}/arm64.S")
    set(PRK_CYCLE_COUNTER_IMPLEMENTATION "asm-gas-arm64")
else()
    # No Assembly for this target yet (e.g. MinGW): use the C fallback.
    set(PRK_CYCLE_COUNTER_SOURCES "${_prk_c_only}")
    set(PRK_CYCLE_COUNTER_IMPLEMENTATION "c-fallback")
endif()
