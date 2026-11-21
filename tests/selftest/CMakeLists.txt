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
    test_junit_reporting.cpp
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

# Child program for the process-spawn tests (#13). Same directory as the self-test (also per config
# with Visual Studio generators), which finds it next to its own executable.
add_executable(prk_spawn_child prk_spawn_child.c)
prk_apply_project_warnings(prk_spawn_child)
add_dependencies(proofrunkit_selftest prk_spawn_child)

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
# Per-test wall-clock timeout from the command line (#12).
add_test(NAME prk_lua_timeout_option COMMAND "${_prk_short_exe}" run --timeout 200 "${_prk_lua_tests}/samples/infinite_loop.lua")
set_tests_properties(prk_lua_timeout_option PROPERTIES PASS_REGULAR_EXPRESSION "timeout of 200 ms exceeded" TIMEOUT 60)
# JavaScript test scripts run through the shipped prk executable on the embedded QuickJS-ng (#11).
set(_prk_js_tests "${PROJECT_SOURCE_DIR}/tests/js")
add_test(NAME prk_js_api_selftest COMMAND "${_prk_short_exe}" run "${_prk_js_tests}/prk_js_api_test.js")
set_tests_properties(prk_js_api_selftest PROPERTIES PASS_REGULAR_EXPRESSION "8 tests: 7 passed, 0 failed, 1 skipped")
add_test(NAME prk_js_api_selftest_exit_code COMMAND "${_prk_short_exe}" run "${_prk_js_tests}/prk_js_api_test.js")
add_test(NAME prk_js_failure_message COMMAND "${_prk_short_exe}" run "${_prk_js_tests}/samples/failing_test.js")
set_tests_properties(prk_js_failure_message PROPERTIES PASS_REGULAR_EXPRESSION
                     "failing_test\\.js:3: deliberate failure: expected 3, got 2")
add_test(NAME prk_js_reports_failures COMMAND "${_prk_short_exe}" run "${_prk_js_tests}/samples/failing_test.js")
set_tests_properties(prk_js_reports_failures PROPERTIES WILL_FAIL TRUE)
add_test(NAME prk_js_reports_syntax_error COMMAND "${_prk_short_exe}" run "${_prk_js_tests}/samples/syntax_error.js")
set_tests_properties(prk_js_reports_syntax_error PROPERTIES PASS_REGULAR_EXPRESSION
                     "syntax_error\\.js:[0-9]+: SyntaxError")
add_test(NAME prk_js_stops_infinite_loop COMMAND "${_prk_short_exe}" run "${_prk_js_tests}/samples/infinite_loop.js")
set_tests_properties(prk_js_stops_infinite_loop PROPERTIES PASS_REGULAR_EXPRESSION
                     "instruction limit of [0-9]+ instructions exceeded" TIMEOUT 60)
add_test(NAME prk_js_timeout_option COMMAND "${_prk_short_exe}" run --timeout 200 "${_prk_js_tests}/samples/infinite_loop.js")
set_tests_properties(prk_js_timeout_option PROPERTIES PASS_REGULAR_EXPRESSION "timeout of 200 ms exceeded" TIMEOUT 60)
add_test(NAME prk_mixed_lua_and_js_list
    COMMAND "${_prk_short_exe}" list "${_prk_lua_tests}/prk_lua_api_test.lua" "${_prk_js_tests}/prk_js_api_test.js")
set_tests_properties(prk_mixed_lua_and_js_list PROPERTIES PASS_REGULAR_EXPRESSION
                     "lua_api\\.reports_versions.*js_suite\\.hooks_run_in_order")

# Report files and the exit-code contract, end to end through prk (#21).
add_test(NAME prk_reports_and_exit_codes
    COMMAND ${CMAKE_COMMAND} "-DPRK_EXE=${_prk_short_exe}" "-DPRK_SOURCE_DIR=${PROJECT_SOURCE_DIR}"
            "-DPRK_WORK_DIR=${CMAKE_CURRENT_BINARY_DIR}/reports" -P "${PROJECT_SOURCE_DIR}/tests/cmake/CheckReportsAndExitCodes.cmake")

# Self-hosted suite (#55): ProofRunKit tests itself with Lua scripts under tests/lua/self, run by the
# shipped prk executable. Each passes on exit code 0 (no failed tests, no load error).
# proofrunkit_selftest (the C++ harness) stays as the fallback (#27).
set(_prk_self "${_prk_lua_tests}/self")
foreach(_prk_self_script IN ITEMS assertions_test suites_and_hooks_test)
    add_test(NAME prk_self_${_prk_self_script} COMMAND "${_prk_short_exe}" run "${_prk_self}/${_prk_self_script}.lua")
endforeach()
add_test(NAME prk_self_timing_and_files_test COMMAND "${_prk_short_exe}" run "${_prk_self}/timing_and_files_test.lua")
set_tests_properties(prk_self_timing_and_files_test PROPERTIES WORKING_DIRECTORY "${PROJECT_SOURCE_DIR}")
add_test(NAME prk_self_tags_test
    COMMAND "${_prk_short_exe}" run --tag selected --exclude-tag excluded "${_prk_self}/tags_test.lua")
set_tests_properties(prk_self_tags_test PROPERTIES PASS_REGULAR_EXPRESSION "3 tests: 2 passed, 0 failed, 1 skipped")

# Process spawn from scripts (#12 over #13): run in prk's folder so the scripts can start "./prk".
add_test(NAME prk_self_process_test COMMAND "${_prk_short_exe}" run "${_prk_self}/process_test.lua")
set_tests_properties(prk_self_process_test PROPERTIES WORKING_DIRECTORY "$<TARGET_FILE_DIR:proofrunkit>")
add_test(NAME prk_js_process_test COMMAND "${_prk_short_exe}" run "${_prk_js_tests}/process_test.js")
set_tests_properties(prk_js_process_test PROPERTIES WORKING_DIRECTORY "$<TARGET_FILE_DIR:proofrunkit>")

# C/C++ compiler driver (#14): the scripts get the samples folder at configure time and run in the build
# folder, where the programs they compile are written. Each test skips when no compiler is on PATH.
set(PRK_C_DRIVER_SAMPLES "${PROJECT_SOURCE_DIR}/tests/drivers/c")
configure_file("${_prk_self}/compile_test.lua.in" "${CMAKE_CURRENT_BINARY_DIR}/compile_test.lua" @ONLY)
configure_file("${_prk_js_tests}/compile_test.js.in" "${CMAKE_CURRENT_BINARY_DIR}/compile_test.js" @ONLY)
add_test(NAME prk_self_compile_test COMMAND "${_prk_short_exe}" run "${CMAKE_CURRENT_BINARY_DIR}/compile_test.lua")
set_tests_properties(prk_self_compile_test PROPERTIES WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}")
add_test(NAME prk_js_compile_test COMMAND "${_prk_short_exe}" run "${CMAKE_CURRENT_BINARY_DIR}/compile_test.js")
set_tests_properties(prk_js_compile_test PROPERTIES WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}")

# Reports: fixture runs write JSON and JUnit files, then reports_test.lua checks them.
set(_prk_self_reports_dir "${CMAKE_CURRENT_BINARY_DIR}/self-reports-run")
file(MAKE_DIRECTORY "${_prk_self_reports_dir}/self-reports")
add_test(NAME prk_self_reports_fixture
    COMMAND "${_prk_short_exe}" run --filter fixture. --exclude-tag slow --json-file self-reports/fixture.json
            --junit-file self-reports/fixture.xml "${_prk_self}/fixtures/report_fixture.lua")
set_tests_properties(prk_self_reports_fixture PROPERTIES WORKING_DIRECTORY "${_prk_self_reports_dir}"
                     WILL_FAIL TRUE FIXTURES_SETUP prk_self_reports)
add_test(NAME prk_self_reports_builtin COMMAND "${_prk_short_exe}" run --json-file self-reports/builtin.json)
set_tests_properties(prk_self_reports_builtin PROPERTIES WORKING_DIRECTORY "${_prk_self_reports_dir}"
                     FIXTURES_SETUP prk_self_reports)
add_test(NAME prk_self_reports_test COMMAND "${_prk_short_exe}" run "${_prk_self}/reports_test.lua")
set_tests_properties(prk_self_reports_test PROPERTIES WORKING_DIRECTORY "${_prk_self_reports_dir}"
                     FIXTURES_REQUIRED prk_self_reports)
