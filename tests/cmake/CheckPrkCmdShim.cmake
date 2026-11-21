# CTest check for the Windows `prk` command (#93): installer/windows/prk.cmd must run proofrunkit.exe
# with every argument (including ones with spaces), and hand back its exit code to cmd and PowerShell.
# Usage: cmake -DPRK_SHIM=<dir>/prk.cmd -DPRK_VERSION=<x.y.z> -DPRK_SOURCE_DIR=<repo> -DPRK_WORK_DIR=<dir>
#              [-DPRK_POWERSHELL=<powershell.exe>] -P CheckPrkCmdShim.cmake
if(NOT EXISTS "${PRK_SHIM}")
    message(FATAL_ERROR "prk.cmd: shim not found at ${PRK_SHIM}")
endif()
get_filename_component(_prk_dir "${PRK_SHIM}" DIRECTORY)
if(NOT EXISTS "${_prk_dir}/proofrunkit.exe")
    message(FATAL_ERROR "prk.cmd: proofrunkit.exe is not next to the shim in ${_prk_dir}")
endif()
if(EXISTS "${_prk_dir}/prk.exe")
    message(FATAL_ERROR "prk.cmd: a prk.exe is next to the shim; only proofrunkit.exe may ship (#93)")
endif()

file(REMOVE_RECURSE "${PRK_WORK_DIR}")
set(_prk_spaced "${PRK_WORK_DIR}/folder with spaces")
file(MAKE_DIRECTORY "${_prk_spaced}")
file(COPY_FILE "${PRK_SOURCE_DIR}/tests/lua/prk_lua_api_test.lua" "${_prk_spaced}/api test.lua")
set(_prk_failing "${PRK_SOURCE_DIR}/tests/lua/samples/failing_test.lua")

# Runs the shim directly (Windows starts .cmd files through cmd.exe) and checks the exit code and output.
function(prk_shim_expect expected_code pattern label)
    execute_process(COMMAND "${PRK_SHIM}" ${ARGN} RESULT_VARIABLE _code OUTPUT_VARIABLE _out ERROR_VARIABLE _err
                    WORKING_DIRECTORY "${PRK_WORK_DIR}")
    if(NOT "${_code}" STREQUAL "${expected_code}")
        message(FATAL_ERROR "prk.cmd: ${label}: expected exit code ${expected_code}, got ${_code}\n${_out}\n${_err}")
    endif()
    if(NOT "${_out}${_err}" MATCHES "${pattern}")
        message(FATAL_ERROR "prk.cmd: ${label}: output does not match '${pattern}'\n${_out}\n${_err}")
    endif()
endfunction()

prk_shim_expect(0 "ProofRunKit ${PRK_VERSION}" "version" -v -offline)
prk_shim_expect(2 "prk: unknown option" "usage error keeps exit code 2" --no-such-option)
prk_shim_expect(1 "deliberate failure" "failing test keeps exit code 1" run "${_prk_failing}")
prk_shim_expect(0 "lua_api\\." "argument with spaces" list "${_prk_spaced}/api test.lua")
prk_shim_expect(0 "Usage: prk run" "help names the prk command" help run)

# PowerShell resolves prk.cmd the same way it does on PATH and reports the exit code in $LASTEXITCODE.
if(PRK_POWERSHELL)
    function(prk_powershell_expect expected_code label script)
        execute_process(COMMAND "${PRK_POWERSHELL}" -NoLogo -NoProfile -NonInteractive -Command "${script}; exit $LASTEXITCODE"
                        RESULT_VARIABLE _code OUTPUT_VARIABLE _out ERROR_VARIABLE _err WORKING_DIRECTORY "${PRK_WORK_DIR}")
        if(NOT "${_code}" STREQUAL "${expected_code}")
            message(FATAL_ERROR "prk.cmd: PowerShell ${label}: expected exit code ${expected_code}, got ${_code}\n${_out}\n${_err}")
        endif()
    endfunction()
    file(TO_NATIVE_PATH "${PRK_SHIM}" _prk_shim_native)
    file(TO_NATIVE_PATH "${_prk_spaced}/api test.lua" _prk_spaced_native)
    prk_powershell_expect(0 "version" "& '${_prk_shim_native}' -v -offline")
    prk_powershell_expect(2 "usage error" "& '${_prk_shim_native}' --no-such-option")
    prk_powershell_expect(0 "argument with spaces" "& '${_prk_shim_native}' list '${_prk_spaced_native}'")
else()
    message(STATUS "prk.cmd: PowerShell not found; skipped the PowerShell checks")
endif()
message(STATUS "prk.cmd: all shim checks passed")
