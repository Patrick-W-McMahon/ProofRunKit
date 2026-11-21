# ProofRunKit: a single, stable test executable with a C/C++ core and Assembly hot paths.
# Windows (MSVC, x64) is the first supported build and test host. Linux and macOS configure/build
# paths exist for future updates but are untested (#34, #35).
cmake_minimum_required(VERSION 3.21)

project(ProofRunKit
    VERSION 0.1.0
    DESCRIPTION "Single high-performance test executable core in C/C++ with Assembly hot paths"
    LANGUAGES C CXX)

option(PRK_FORCE_C_FALLBACK "Use the portable C fallback instead of Assembly for every hot path" OFF)
option(PRK_WARNINGS_AS_ERRORS "Treat compiler warnings as errors" OFF)
option(PRK_REPRODUCIBLE_BUILD "Ask the toolchain for reproducible output (MSVC: /Brepro)" ON)
option(PRK_BUILD_SELFTEST "Build proofrunkit_selftest and register it with CTest" ON)

set(CMAKE_C_STANDARD 17)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")
include(ProofRunKitWarnings)
include(ProofRunKitCycleCounter)

# --------------------------------------------------------------------------- core (C17 + Assembly)
add_library(prk_core STATIC
    src/core/prk_registry.c
    src/core/prk_runner.c
    src/core/prk_summary.c
    src/core/prk_version.c
    src/timing/prk_cycle_counter_fallback.c
    src/timing/prk_cycle_counter_info.c
    ${PRK_CYCLE_COUNTER_SOURCES})
target_include_directories(prk_core PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/include")
target_compile_definitions(prk_core PRIVATE
    PRK_VERSION_STRING="${PROJECT_VERSION}"
    PRK_CYCLE_COUNTER_IMPLEMENTATION="${PRK_CYCLE_COUNTER_IMPLEMENTATION}")
prk_apply_project_warnings(prk_core)

# ------------------------------------------------------------- process-spawn layer (#13)
# One operating-system adapter per build; the portable part is shared and unit-tested everywhere.
if(WIN32)
    set(_prk_process_adapter src/process/prk_process_windows.c)
else()
    set(_prk_process_adapter src/process/prk_process_posix.c)
endif()
add_library(prk_process STATIC src/process/prk_process.c ${_prk_process_adapter})
target_link_libraries(prk_process PUBLIC prk_core)
prk_apply_project_warnings(prk_process)

# ------------------------------------------------------------- language drivers (#14 ...)
add_library(prk_drivers STATIC src/drivers/prk_c_driver.c src/drivers/prk_c_arguments.c src/drivers/prk_c_diagnostics.c
    src/drivers/prk_msvc_toolchain.c src/drivers/prk_asm_driver.c)
target_include_directories(prk_drivers PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src")
target_link_libraries(prk_drivers PUBLIC prk_core prk_process)
prk_apply_project_warnings(prk_drivers)

# ------------------------------------------------------- embedded Lua 5.4 test-script engine (#9)
include(ProofRunKitLua)
include(ProofRunKitQuickJs)
add_library(prk_script STATIC
    src/script/prk_lua_api.c
    src/script/prk_lua_process.c
    src/script/prk_lua_compile.c
    src/script/prk_js_compile.c
    src/script/prk_js_options.c
    src/script/prk_js_assemble.c
    src/script/prk_js_process.c
    src/script/prk_script.c
    src/script/prk_test_plan.c
    src/script/prk_lua_sandbox.c
    src/script/prk_lua_script.c
    src/script/prk_js_api.c
    src/script/prk_js_script.c)
target_link_libraries(prk_script PUBLIC prk_core prk_process prk_drivers PRIVATE prk_lua prk_quickjs)
prk_apply_project_warnings(prk_script)

# ------------------------------------------------------------- front end (C++20 CLI + reporters)
add_library(prk_frontend STATIC
    src/cpp/builtin_checks.cpp
    src/cpp/command_line.cpp
    src/cpp/command_line_app.cpp
    src/cpp/junit_reporter.cpp
    src/cpp/reporters.cpp
    src/cpp/script_host.cpp
    src/cpp/script_loading.cpp)
target_include_directories(prk_frontend PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/src")
target_link_libraries(prk_frontend PUBLIC prk_core prk_script prk_process)
prk_apply_project_warnings(prk_frontend)

# ------------------------------------------------------------- the single proofrunkit executable
add_executable(proofrunkit src/main.cpp)
target_link_libraries(proofrunkit PRIVATE prk_frontend)
prk_apply_project_warnings(proofrunkit)
include(ProofRunKitWindowsResources)
prk_add_windows_resources(proofrunkit)

# The short `prk` command (#60) is a byte-identical copy of the same build output, not a second
# compiled program: one file hash for antivirus reputation (#22), and the MSIs install both names.
set(PRK_SHORT_EXECUTABLE_NAME "prk${CMAKE_EXECUTABLE_SUFFIX}")
add_custom_command(TARGET proofrunkit POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different "$<TARGET_FILE:proofrunkit>"
            "$<TARGET_FILE_DIR:proofrunkit>/${PRK_SHORT_EXECUTABLE_NAME}"
    COMMENT "Copying proofrunkit to ${PRK_SHORT_EXECUTABLE_NAME} (same binary, #60)"
    VERBATIM)

# ------------------------------------------------------------------------------- self-test (#23)
if(PRK_BUILD_SELFTEST)
    enable_testing()
    add_subdirectory(tests/selftest)
endif()

# ---------------------------------------------------------- Windows MSI (#37, #38, #40)
# Windows only. The target is not part of ALL; build it with --target package-msi.
# An x64 tree packages ProofRunKit-<version>-win-x64.msi; a Win32 tree packages
# ProofRunKit-<version>-win-x86.msi.
if(WIN32)
    include(ProofRunKitMsi)
endif()

message(STATUS "ProofRunKit ${PROJECT_VERSION}: cycle counter = ${PRK_CYCLE_COUNTER_IMPLEMENTATION}")
