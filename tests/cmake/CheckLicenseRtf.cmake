# CTest check for cmake/ProofRunKitLicenseRtf.cmake (#58): the installer license dialog RTF is
# generated from LICENSE, escapes RTF control characters, and ignores CR line endings.
# Usage: cmake -DPRK_SOURCE_DIR=<repo> -DPRK_WORK_DIR=<dir> -P CheckLicenseRtf.cmake
include("${PRK_SOURCE_DIR}/cmake/ProofRunKitLicenseRtf.cmake")
file(MAKE_DIRECTORY "${PRK_WORK_DIR}")

function(prk_expect_contains haystack needle label)
    string(FIND "${haystack}" "${needle}" _prk_at)
    if(_prk_at EQUAL -1)
        message(FATAL_ERROR "license-rtf: ${label}: expected to find '${needle}'")
    endif()
endfunction()

# The real LICENSE converts into a complete RTF document.
prk_write_license_rtf("${PRK_SOURCE_DIR}/LICENSE" "${PRK_WORK_DIR}/License.rtf")
file(READ "${PRK_WORK_DIR}/License.rtf" _prk_rtf)
string(FIND "${_prk_rtf}" "{\\rtf1\\ansi" _prk_header_at)
if(NOT _prk_header_at EQUAL 0)
    message(FATAL_ERROR "license-rtf: output does not start with an RTF header")
endif()
prk_expect_contains("${_prk_rtf}" "Apache License\\par" "title paragraph")
prk_expect_contains("${_prk_rtf}" "END OF TERMS AND CONDITIONS\\par" "end of terms")

# Control characters are escaped and CRLF input matches LF input.
file(WRITE "${PRK_WORK_DIR}/sample-lf.txt" "a\\b {c}\nline two\n")
file(WRITE "${PRK_WORK_DIR}/sample-crlf.txt" "a\\b {c}\r\nline two\r\n")
prk_write_license_rtf("${PRK_WORK_DIR}/sample-lf.txt" "${PRK_WORK_DIR}/sample-lf.rtf")
prk_write_license_rtf("${PRK_WORK_DIR}/sample-crlf.txt" "${PRK_WORK_DIR}/sample-crlf.rtf")
file(READ "${PRK_WORK_DIR}/sample-lf.rtf" _prk_lf)
file(READ "${PRK_WORK_DIR}/sample-crlf.rtf" _prk_crlf)
prk_expect_contains("${_prk_lf}" "a\\\\b \\{c\\}\\par\nline two\n}" "escaped sample")
if(NOT _prk_lf STREQUAL _prk_crlf)
    message(FATAL_ERROR "license-rtf: CRLF input produced different RTF than LF input")
endif()
message(STATUS "license-rtf: all checks passed")
