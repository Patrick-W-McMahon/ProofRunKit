# proofrunkit_selftest (#23): the core's own unit tests, run through the same registry, runner, and
# reporters that ProofRunKit uses for user tests.
add_executable(proofrunkit_selftest
    selftest_main.cpp
    test_command_line.cpp
    test_cycle_counter.cpp
    test_lua_engine.cpp
    test_registry.cpp
    test_reporting.cpp
    test_runner.cpp
    test_script_loading.cpp)
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

# The short `prk` command (#60): the same binary under a second name, with subcommands.
set(_prk_short_exe "$<TARGET_FILE_DIR:proofrunkit>/${PRK_SHORT_EXECUTABLE_NAME}")
add_test(NAME prk_is_identical_copy
    COMMAND ${CMAKE_COMMAND} -E compare_files "$<TARGET_FILE:proofrunkit>" "${_prk_short_exe}")
add_test(NAME prk_version COMMAND "${_prk_short_exe}" --version)
set_tests_properties(prk_version PROPERTIES PASS_REGULAR_EXPRESSION "ProofRunKit ${PROJECT_VERSION}")
add_test(NAME prk_version_subcommand COMMAND "${_prk_short_exe}" version)
set_tests_properties(prk_version_subcommand PROPERTIES PASS_REGULAR_EXPRESSION "ProofRunKit ${PROJECT_VERSION}")
add_test(NAME prk_help_run COMMAND "${_prk_short_exe}" help run)
set_tests_properties(prk_help_run PROPERTIES PASS_REGULAR_EXPRESSION "Usage: prk run")
add_test(NAME prk_list COMMAND "${_prk_short_exe}" list)
set_tests_properties(prk_list PROPERTIES PASS_REGULAR_EXPRESSION "kit\\.")
add_test(NAME prk_run_builtin_checks_json COMMAND "${_prk_short_exe}" run --json)
set_tests_properties(prk_run_builtin_checks_json PROPERTIES PASS_REGULAR_EXPRESSION "\"failed\": 0")
add_test(NAME prk_rejects_unknown_command COMMAND "${_prk_short_exe}" help no-such-command)
set_tests_properties(prk_rejects_unknown_command PROPERTIES PASS_REGULAR_EXPRESSION "prk: unknown command")

# The installer license dialog RTF is generated from the repo-root LICENSE (#58).
add_test(NAME proofrunkit_license_rtf_generation
    COMMAND ${CMAKE_COMMAND} "-DPRK_SOURCE_DIR=${PROJECT_SOURCE_DIR}"
            "-DPRK_WORK_DIR=${CMAKE_CURRENT_BINARY_DIR}/license-rtf" -P "${PROJECT_SOURCE_DIR}/tests/cmake/CheckLicenseRtf.cmake")

# Lua test scripts run through the shipped prk executable (#9; first self-hosted tests, #55).
set(_prk_lua_tests "${PROJECT_SOURCE_DIR}/tests/lua")
add_test(NAME prk_lua_api_selftest COMMAND "${_prk_short_exe}" run "${_prk_lua_tests}/prk_lua_api_test.lua")
set_tests_properties(prk_lua_api_selftest PROPERTIES PASS_REGULAR_EXPRESSION "8 tests: 7 passed, 0 failed, 1 skipped")
add_test(NAME prk_lua_api_selftest_exit_code COMMAND "${_prk_short_exe}" run "${_prk_lua_tests}/prk_lua_api_test.lua")
add_test(NAME prk_lua_list COMMAND "${_prk_short_exe}" list "${_prk_lua_tests}/prk_lua_api_test.lua")
set_tests_properties(prk_lua_list PROPERTIES PASS_REGULAR_EXPRESSION "lua_sandbox\\.load_accepts_text_and_rejects_bytecode")
add_test(NAME prk_lua_reports_failures COMMAND "${_prk_short_exe}" run "${_prk_lua_tests}/samples/failing_test.lua")
set_tests_properties(prk_lua_reports_failures PROPERTIES WILL_FAIL TRUE)
add_test(NAME prk_lua_failure_message COMMAND "${_prk_short_exe}" run "${_prk_lua_tests}/samples/failing_test.lua")
set_tests_properties(prk_lua_failure_message PROPERTIES PASS_REGULAR_EXPRESSION "deliberate failure: expected 3, got 2")
add_test(NAME prk_lua_reports_syntax_error COMMAND "${_prk_short_exe}" run "${_prk_lua_tests}/samples/syntax_error.lua")
set_tests_properties(prk_lua_reports_syntax_error PROPERTIES PASS_REGULAR_EXPRESSION "syntax_error\\.lua:[0-9]+:")
add_test(NAME prk_lua_reports_missing_file COMMAND "${_prk_short_exe}" run "${_prk_lua_tests}/samples/no_such_file.lua")
set_tests_properties(prk_lua_reports_missing_file PROPERTIES PASS_REGULAR_EXPRESSION "no_such_file\\.lua: cannot open file")
add_test(NAME prk_lua_stops_infinite_loop COMMAND "${_prk_short_exe}" run "${_prk_lua_tests}/samples/infinite_loop.lua")
set_tests_properties(prk_lua_stops_infinite_loop PROPERTIES PASS_REGULAR_EXPRESSION "instruction limit of [0-9]+ instructions exceeded" TIMEOUT 60)
