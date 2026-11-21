# Converts the repo-root LICENSE (plain text) into the RTF shown by the WiX license dialog (#58).
#
# Generating the RTF from LICENSE keeps a single license source: the installer dialog and the
# packaged LICENSE.txt can never drift from the verbatim Apache-2.0 text GitHub detects (#3).
#
# Use either as an include (call prk_write_license_rtf) or as a script:
#   cmake -DPRK_LICENSE_INPUT=LICENSE -DPRK_LICENSE_RTF_OUTPUT=License.rtf -P ProofRunKitLicenseRtf.cmake

# Writes @p output_rtf: a Courier New RTF document whose paragraphs are the lines of @p input_text.
# RTF control characters (\ { }) are escaped; CR characters are dropped so CRLF checkouts produce
# the same RTF as LF checkouts. The file is rewritten only when its content changes.
function(prk_write_license_rtf input_text output_rtf)
    file(READ "${input_text}" _prk_text)
    string(REPLACE "\r" "" _prk_text "${_prk_text}")
    string(REPLACE "\\" "\\\\" _prk_text "${_prk_text}")
    string(REPLACE "{" "\\{" _prk_text "${_prk_text}")
    string(REPLACE "}" "\\}" _prk_text "${_prk_text}")
    # Drop one trailing newline so the document does not end with an empty paragraph.
    string(REGEX REPLACE "\n$" "" _prk_text "${_prk_text}")
    string(REPLACE "\n" "\\par\n" _prk_text "${_prk_text}")
    set(_prk_rtf "{\\rtf1\\ansi\\ansicpg1252\\deff0\\nouicompat\\fs20\n{\\fonttbl{\\f0\\fmodern\\fcharset0 Courier New;}}\n\\f0\\fs18\n${_prk_text}\n}\n")
    if(EXISTS "${output_rtf}")
        file(READ "${output_rtf}" _prk_existing)
        if(_prk_existing STREQUAL _prk_rtf)
            return()
        endif()
    endif()
    file(WRITE "${output_rtf}" "${_prk_rtf}")
endfunction()

if(CMAKE_SCRIPT_MODE_FILE AND PRK_LICENSE_INPUT AND PRK_LICENSE_RTF_OUTPUT)
    prk_write_license_rtf("${PRK_LICENSE_INPUT}" "${PRK_LICENSE_RTF_OUTPUT}")
endif()
