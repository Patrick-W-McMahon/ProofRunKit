# Invoked by the package-msi target (cmake -P). Builds the x64 or x86 MSI with WiX v4.
# Expected -D variables: PRK_EXE, PRK_VERSION, PRK_WXS, PRK_LICENSE_RTF,
# PRK_LICENSE_TXT, PRK_URL_SHORTCUT, PRK_MSI_OUTPUT, PRK_INTERMEDIATE_DIR,
# optionally PRK_MSI_ARCH (x64 or x86, default x64), and optionally PRK_WIX_EXECUTABLE.

if(NOT PRK_MSI_ARCH)
    set(PRK_MSI_ARCH "x64")
endif()
if(NOT PRK_MSI_ARCH MATCHES "^(x64|x86)$")
    message(FATAL_ERROR "package-msi: PRK_MSI_ARCH must be x64 or x86 (got '${PRK_MSI_ARCH}')")
endif()

if(NOT PRK_EXE OR NOT EXISTS "${PRK_EXE}")
    message(FATAL_ERROR "package-msi: proofrunkit executable not found (PRK_EXE='${PRK_EXE}'). Build the matching windows-msvc-${PRK_MSI_ARCH}-release preset first.")
endif()

foreach(_prk_required IN ITEMS PRK_VERSION PRK_WXS PRK_LICENSE_RTF PRK_LICENSE_TXT PRK_URL_SHORTCUT PRK_MSI_OUTPUT PRK_INTERMEDIATE_DIR)
    if(NOT ${_prk_required})
        message(FATAL_ERROR "package-msi: ${_prk_required} is not set")
    endif()
endforeach()

foreach(_prk_input IN ITEMS PRK_WXS PRK_LICENSE_RTF PRK_LICENSE_TXT PRK_URL_SHORTCUT)
    if(NOT EXISTS "${${_prk_input}}")
        message(FATAL_ERROR "package-msi: missing input ${${_prk_input}}")
    endif()
endforeach()

set(_prk_wix_install_help [=[
The WiX Toolset v4 command-line tool was not found (and the UI extension is required).
The .NET SDK (6 or later; 8 recommended) is required. Install it from https://dot.net
or with:  winget install Microsoft.DotNet.SDK.8

Then install WiX v4 and the UI extension (do not omit the version pins):

  dotnet tool install --global wix --version 4.0.6
  wix extension add -g WixToolset.UI.wixext --version 4.0.6

Close and reopen the terminal so %USERPROFILE%\.dotnet\tools is on PATH, then rebuild
the package-msi target.
]=])

if(PRK_WIX_EXECUTABLE AND EXISTS "${PRK_WIX_EXECUTABLE}")
    set(_prk_wix "${PRK_WIX_EXECUTABLE}")
else()
    find_program(_prk_wix NAMES wix wix.exe
        PATHS
            "$ENV{USERPROFILE}/.dotnet/tools"
            "$ENV{HOME}/.dotnet/tools")
endif()

if(NOT _prk_wix)
    message(FATAL_ERROR "${_prk_wix_install_help}")
endif()
set(PRK_WIX_EXECUTABLE "${_prk_wix}")

# Confirm this is WiX 4.x (Patrick's chosen toolset, still published as 4.0.6).
execute_process(
    COMMAND "${PRK_WIX_EXECUTABLE}" --version
    OUTPUT_VARIABLE _prk_wix_version_raw
    ERROR_VARIABLE _prk_wix_version_err
    RESULT_VARIABLE _prk_wix_version_rc
    OUTPUT_STRIP_TRAILING_WHITESPACE
    ERROR_STRIP_TRAILING_WHITESPACE)
if(NOT _prk_wix_version_rc EQUAL 0)
    message(FATAL_ERROR "package-msi: '${PRK_WIX_EXECUTABLE} --version' failed:\n${_prk_wix_version_err}\n${_prk_wix_install_help}")
endif()
set(_prk_wix_version "${_prk_wix_version_raw}")
# wix --version may print just "4.0.6" or a longer banner; keep the first token.
string(REGEX REPLACE "[ \t\r\n].*$" "" _prk_wix_version "${_prk_wix_version}")
string(REGEX REPLACE "^v" "" _prk_wix_version "${_prk_wix_version}")
if(NOT _prk_wix_version MATCHES "^4\\.")
    message(FATAL_ERROR
        "package-msi: WiX ${_prk_wix_version_raw} is not v4. This project pins WiX Toolset v4 (4.0.6),\n"
        "which is still published on NuGet. Uninstall other versions if needed, then run:\n\n"
        "  dotnet tool uninstall --global wix\n"
        "  dotnet tool install --global wix --version 4.0.6\n"
        "  wix extension add -g WixToolset.UI.wixext --version 4.0.6\n")
endif()

# Forward slashes avoid backslash-escape issues in WiX preprocessor defines.
file(TO_CMAKE_PATH "${PRK_EXE}" _prk_exe)
file(TO_CMAKE_PATH "${PRK_LICENSE_RTF}" _prk_license_rtf)
file(TO_CMAKE_PATH "${PRK_LICENSE_TXT}" _prk_license_txt)
file(TO_CMAKE_PATH "${PRK_URL_SHORTCUT}" _prk_url_shortcut)
get_filename_component(_prk_msi_dir "${PRK_MSI_OUTPUT}" DIRECTORY)
file(MAKE_DIRECTORY "${_prk_msi_dir}")
file(MAKE_DIRECTORY "${PRK_INTERMEDIATE_DIR}")

message(STATUS "package-msi: WiX ${_prk_wix_version_raw} at ${PRK_WIX_EXECUTABLE}")
message(STATUS "package-msi: executable ${PRK_EXE}")
message(STATUS "package-msi: output     ${PRK_MSI_OUTPUT}")

execute_process(
    COMMAND "${PRK_WIX_EXECUTABLE}" build
        -arch ${PRK_MSI_ARCH}
        -ext WixToolset.UI.wixext
        -d "PrkVersion=${PRK_VERSION}"
        -d "PrkExePath=${_prk_exe}"
        -d "PrkLicenseRtf=${_prk_license_rtf}"
        -d "PrkLicenseTxt=${_prk_license_txt}"
        -d "PrkUrlShortcut=${_prk_url_shortcut}"
        -intermediateFolder "${PRK_INTERMEDIATE_DIR}"
        -o "${PRK_MSI_OUTPUT}"
        "${PRK_WXS}"
    RESULT_VARIABLE _prk_wix_build_rc
    OUTPUT_VARIABLE _prk_wix_build_out
    ERROR_VARIABLE _prk_wix_build_err
    ECHO_OUTPUT_VARIABLE
    ECHO_ERROR_VARIABLE)
if(NOT _prk_wix_build_rc EQUAL 0)
    message(FATAL_ERROR
        "package-msi: wix build failed with exit code ${_prk_wix_build_rc}.\n"
        "${_prk_wix_build_out}\n${_prk_wix_build_err}\n"
        "If the error mentions WixToolset.UI.wixext or an extension, run:\n"
        "  wix extension add -g WixToolset.UI.wixext --version 4.0.6\n")
endif()

if(NOT EXISTS "${PRK_MSI_OUTPUT}")
    message(FATAL_ERROR "package-msi: wix build reported success but '${PRK_MSI_OUTPUT}' was not created")
endif()

message(STATUS "package-msi: built ${PRK_MSI_OUTPUT}")
