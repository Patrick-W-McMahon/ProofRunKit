# proofrunkit_selftest (#23): the core's own unit tests, run through the same registry, runner, and
# reporters that ProofRunKit uses for user tests.
add_executable(proofrunkit_selftest
    selftest_main.cpp
    test_command_line.cpp
    test_cycle_counter.cpp
    test_js_engine.cpp
    test_lua_engine.cpp
    test_lua_test_api.cpp
    test_process_spawn.cpp
    test_script_process_api.cpp
    test_c_driver.cpp
    test_c_driver_toolchain.cpp
    test_msvc_discovery_text.cpp
    test_asm_driver.cpp
    test_release_info.cpp
    test_update_command.cpp
    test_junit_reporting.cpp
    test_registry.cpp
    test_reporting.cpp
    test_runner.cpp
    test_script_loading.cpp
    test_suite_config.cpp
    test_suite_runner.cpp)
target_link_libraries(proofrunkit_selftest PRIVATE prk_frontend)
prk_apply_project_warnings(proofrunkit_selftest)
prk_add_windows_resources(proofrunkit_selftest)
if(PRK_FORCE_C_FALLBACK)
    target_compile_definitions(proofrunkit_selftest PRIVATE PRK_EXPECT_C_FALLBACK)
endif()

# Child program for the process-spawn tests (#13). Same directory as the self-test (also per config
# with Visual Studio generators), which finds it next to its own executable.
add_executable(prk_spawn_child prk_spawn_child.c)
target_link_libraries(prk_spawn_child PRIVATE prk_process)
prk_apply_project_warnings(prk_spawn_child)
add_dependencies(proofrunkit_selftest prk_spawn_child)

add_test(NAME proofrunkit_selftest COMMAND proofrunkit_selftest)

# The short `prk` command (#60): the same binary under a second name, with subcommands.
set(_prk_short_exe "$<TARGET_FILE_DIR:proofrunkit>/${PRK_SHORT_EXECUTABLE_NAME}")
add_test(NAME prk_is_identical_copy
    COMMAND ${CMAKE_COMMAND} -E compare_files "$<TARGET_FILE:proofrunkit>" "${_prk_short_exe}")

# The installer license dialog RTF is generated from the repo-root LICENSE (#58).
add_test(NAME proofrunkit_license_rtf_generation
    COMMAND ${CMAKE_COMMAND} "-DPRK_SOURCE_DIR=${PROJECT_SOURCE_DIR}"
            "-DPRK_WORK_DIR=${CMAKE_CURRENT_BINARY_DIR}/license-rtf" -P "${PROJECT_SOURCE_DIR}/tests/cmake/CheckLicenseRtf.cmake")

# Report files and the exit-code contract, end to end through prk (#21).
add_test(NAME prk_reports_and_exit_codes
    COMMAND ${CMAKE_COMMAND} "-DPRK_EXE=${_prk_short_exe}" "-DPRK_SOURCE_DIR=${PROJECT_SOURCE_DIR}"
            "-DPRK_WORK_DIR=${CMAKE_CURRENT_BINARY_DIR}/reports" -P "${PROJECT_SOURCE_DIR}/tests/cmake/CheckReportsAndExitCodes.cmake")

# Every script, smoke, and sample test lives in prk.tests.json at the repo root and runs through prk itself
# (#83): `prk test` from the repo root runs the same suite outside CTest, also with the installed prk. CTest only
# points it at this build; the folders are passed explicitly so any generator (Ninja, Visual Studio) works.
add_test(NAME prk_test_suite
    COMMAND "${_prk_short_exe}" test --config "${PROJECT_SOURCE_DIR}/prk.tests.json" --build-dir "${PROJECT_BINARY_DIR}"
            --var "bin_dir=$<TARGET_FILE_DIR:proofrunkit>" --var "selftest_dir=$<TARGET_FILE_DIR:prk_spawn_child>"
            --var "work_dir=${CMAKE_CURRENT_BINARY_DIR}/prk-test")
set_tests_properties(prk_test_suite PROPERTIES TIMEOUT 1800)

# No network in CTest (#75): `prk -version` would otherwise look up the latest GitHub release.
get_property(_prk_all_tests DIRECTORY PROPERTY TESTS)
set_property(TEST ${_prk_all_tests} APPEND PROPERTY ENVIRONMENT "PRK_OFFLINE=1")
