# Selects the prk_read_cycle_counter implementation (#8).
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
elseif(NOT WIN32 AND _prk_processor MATCHES "^(x86_64|amd64)$" AND CMAKE_SIZEOF_VOID_P EQUAL 8)
    enable_language(ASM)
    set(PRK_CYCLE_COUNTER_SOURCES "${_prk_asm_dir}/x86_64_sysv.S")
    set(PRK_CYCLE_COUNTER_IMPLEMENTATION "asm-gas-x86_64-sysv")
elseif(NOT WIN32 AND _prk_processor MATCHES "^(aarch64|arm64)$")
    enable_language(ASM)
    set(PRK_CYCLE_COUNTER_SOURCES "${_prk_asm_dir}/arm64.S")
    set(PRK_CYCLE_COUNTER_IMPLEMENTATION "asm-gas-arm64")
else()
    # No Assembly for this target yet (e.g. MinGW, 32-bit x86 until #39): use the C fallback.
    set(PRK_CYCLE_COUNTER_SOURCES "${_prk_c_only}")
    set(PRK_CYCLE_COUNTER_IMPLEMENTATION "c-fallback")
endif()
