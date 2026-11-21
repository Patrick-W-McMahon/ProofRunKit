# proofrunkit_selftest (#23): the core's own unit tests, run through the same registry, runner, and
# reporters that ProofRunKit uses for user tests.
add_executable(proofrunkit_selftest
    selftest_main.cpp
    test_command_line.cpp
    test_cycle_counter.cpp
    test_registry.cpp
    test_reporting.cpp
    test_runner.cpp)
target_link_libraries(proofrunkit_selftest PRIVATE prk_frontend)
prk_apply_project_warnings(proofrunkit_selftest)
prk_add_windows_resources(proofrunkit_selftest)
if(PRK_FORCE_C_FALLBACK)
    target_compile_definitions(proofrunkit_selftest PRIVATE PRK_EXPECT_C_FALLBACK)
endif()

add_test(NAME proofrunkit_selftest COMMAND proofrunkit_selftest)

# Smoke tests of the shipped executable itself.
add_test(NAME proofrunkit_version COMMAND proofrunkit --version)
set_tests_properties(proofrunkit_version PROPERTIES PASS_REGULAR_EXPRESSION "ProofRunKit ${PROJECT_VERSION}")
add_test(NAME proofrunkit_builtin_checks COMMAND proofrunkit)
add_test(NAME proofrunkit_rejects_unknown_option COMMAND proofrunkit --no-such-option)
set_tests_properties(proofrunkit_rejects_unknown_option PROPERTIES PASS_REGULAR_EXPRESSION "unknown option")

# The installer license dialog RTF is generated from the repo-root LICENSE (#58).
add_test(NAME proofrunkit_license_rtf_generation
    COMMAND ${CMAKE_COMMAND} "-DPRK_SOURCE_DIR=${PROJECT_SOURCE_DIR}"
            "-DPRK_WORK_DIR=${CMAKE_CURRENT_BINARY_DIR}/license-rtf" -P "${PROJECT_SOURCE_DIR}/tests/cmake/CheckLicenseRtf.cmake")
