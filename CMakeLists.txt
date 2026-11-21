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

# ------------------------------------------------------------- front end (C++20 CLI + reporters)
add_library(prk_frontend STATIC
    src/cpp/builtin_checks.cpp
    src/cpp/command_line.cpp
    src/cpp/command_line_app.cpp
    src/cpp/reporters.cpp)
target_include_directories(prk_frontend PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/src")
target_link_libraries(prk_frontend PUBLIC prk_core)
prk_apply_project_warnings(prk_frontend)

# ------------------------------------------------------------- the single proofrunkit executable
add_executable(proofrunkit src/main.cpp)
target_link_libraries(proofrunkit PRIVATE prk_frontend)
prk_apply_project_warnings(proofrunkit)
include(ProofRunKitWindowsResources)
prk_add_windows_resources(proofrunkit)

# ------------------------------------------------------------------------------- self-test (#23)
if(PRK_BUILD_SELFTEST)
    enable_testing()
    add_subdirectory(tests/selftest)
endif()

# ---------------------------------------------------------------------- x64 MSI (#37, #40)
# Windows only. The target is not part of ALL; build it with --target package-msi.
# x86 packaging is #38 and is intentionally omitted here.
if(WIN32)
    include(ProofRunKitMsi)
endif()

message(STATUS "ProofRunKit ${PROJECT_VERSION}: cycle counter = ${PRK_CYCLE_COUNTER_IMPLEMENTATION}")
