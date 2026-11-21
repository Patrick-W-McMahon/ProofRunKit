# CTest check for report files and the exit-code contract (#21, docs/exit-codes.md), run against the
# shipped prk executable so the contract is tested end to end.
# Usage: cmake -DPRK_EXE=<prk> -DPRK_SOURCE_DIR=<repo> -DPRK_WORK_DIR=<dir> -P CheckReportsAndExitCodes.cmake
file(REMOVE_RECURSE "${PRK_WORK_DIR}")
file(MAKE_DIRECTORY "${PRK_WORK_DIR}")
set(_prk_samples "${PRK_SOURCE_DIR}/tests/lua/samples")

function(prk_expect_exit_code expected label)
    execute_process(COMMAND "${PRK_EXE}" ${ARGN} RESULT_VARIABLE _prk_code OUTPUT_VARIABLE _prk_out
                    ERROR_VARIABLE _prk_err WORKING_DIRECTORY "${PRK_WORK_DIR}")
    if(NOT "${_prk_code}" STREQUAL "${expected}")
        message(FATAL_ERROR "reports: ${label}: expected exit code ${expected}, got ${_prk_code}\n${_prk_out}\n${_prk_err}")
    endif()
endfunction()

function(prk_expect_file_contains path needle label)
    if(NOT EXISTS "${path}")
        message(FATAL_ERROR "reports: ${label}: ${path} was not written")
    endif()
    file(READ "${path}" _prk_text)
    string(FIND "${_prk_text}" "${needle}" _prk_at)
    if(_prk_at EQUAL -1)
        message(FATAL_ERROR "reports: ${label}: expected '${needle}' in ${path}")
    endif()
endfunction()

# 0: every selected test passed; both report files are written.
prk_expect_exit_code(0 "passing run" run --json-file pass.json --junit-file pass.xml)
prk_expect_file_contains("${PRK_WORK_DIR}/pass.json" "\"schema_version\": 1" "JSON schema version")
prk_expect_file_contains("${PRK_WORK_DIR}/pass.json" "\"failed\": 0" "JSON totals")
prk_expect_file_contains("${PRK_WORK_DIR}/pass.xml" "<testsuites name=\"proofrunkit\"" "JUnit root")
prk_expect_file_contains("${PRK_WORK_DIR}/pass.xml" "failures=\"0\"" "JUnit totals")

# 1: a test failed; the reports still describe the whole run.
prk_expect_exit_code(1 "failing run" run --json-file fail.json --junit-file fail.xml "${_prk_samples}/failing_test.lua")
prk_expect_file_contains("${PRK_WORK_DIR}/fail.json" "\"outcome\": \"failed\"" "JSON failure")
prk_expect_file_contains("${PRK_WORK_DIR}/fail.xml" "<failure message=" "JUnit failure")
prk_expect_file_contains("${PRK_WORK_DIR}/fail.xml" "deliberate failure: expected 3, got 2" "JUnit failure text")

# 2: usage error.  3: not implemented.  4: script error.  5: report not writable (wins over 1).
prk_expect_exit_code(2 "unknown option" run --no-such-option)
prk_expect_exit_code(3 "JavaScript script" run "not_yet_supported.js")
prk_expect_exit_code(4 "syntax error" run "${_prk_samples}/syntax_error.lua")
prk_expect_exit_code(5 "unwritable report" run --junit-file "no_such_dir/sub/report.xml")
prk_expect_exit_code(5 "unwritable report after failures" run --json-file "no_such_dir/r.json"
                     "${_prk_samples}/failing_test.lua")
