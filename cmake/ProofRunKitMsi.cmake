# Defines the package-msi target for the 64-bit WiX v4 installer (#37, #40).
#
# Direct `wix build` is used instead of CPack's WIX generator so the UpgradeCode,
# ProgramFiles64Folder destination, optional PATH feature, Start Menu entries,
# and ARP properties are authored in installer/windows/ProofRunKit-x64.wxs and
# stay reviewable. CPack's WIX generator can target WiX v4 (CMake 3.30+,
# CPACK_WIX_VERSION=4), but the optional PATH feature and documented silent
# flags are simpler to keep correct in first-party authoring.

set(_prk_installer_dir "${CMAKE_CURRENT_LIST_DIR}/../installer/windows")
set(_prk_wxs "${_prk_installer_dir}/ProofRunKit-x64.wxs")
set(_prk_license_rtf "${_prk_installer_dir}/License.rtf")
set(_prk_license_txt "${_prk_installer_dir}/LICENSE.txt")
set(_prk_url_shortcut "${_prk_installer_dir}/ProofRunKit.url")
set(_prk_msi_dir "${CMAKE_BINARY_DIR}/installer")
set(_prk_msi_output "${_prk_msi_dir}/ProofRunKit-${PROJECT_VERSION}-win-x64.msi")
set(_prk_msi_intermediate "${_prk_msi_dir}/wixobj")

# Optional hint; ProofRunKitBuildMsi.cmake also searches PATH and the default
# dotnet tools folder at build time so configure can succeed before WiX is
# installed.
find_program(PRK_WIX_EXECUTABLE
    NAMES wix wix.exe
    PATHS
        "$ENV{USERPROFILE}/.dotnet/tools"
        "$ENV{HOME}/.dotnet/tools"
    DOC "WiX Toolset v4 command-line tool (dotnet tool install --global wix --version 4.0.6)")

if(PRK_WIX_EXECUTABLE)
    message(STATUS "ProofRunKit MSI: WiX tool at ${PRK_WIX_EXECUTABLE}")
else()
    message(STATUS "ProofRunKit MSI: WiX tool not found at configure time; package-msi will search again at build time")
endif()

# Builds ProofRunKit-${PROJECT_VERSION}-win-x64.msi from the Release (or current
# config) proofrunkit executable using WiX v4. Not part of ALL.
function(prk_add_package_msi_target)
    set(_prk_build_msi_script "${CMAKE_CURRENT_FUNCTION_LIST_DIR}/ProofRunKitBuildMsi.cmake")
    add_custom_command(
        OUTPUT "${_prk_msi_output}"
        COMMAND ${CMAKE_COMMAND} -E make_directory "${_prk_msi_dir}"
        COMMAND ${CMAKE_COMMAND} -E make_directory "${_prk_msi_intermediate}"
        COMMAND ${CMAKE_COMMAND}
            "-DPRK_WIX_EXECUTABLE=${PRK_WIX_EXECUTABLE}"
            "-DPRK_EXE=$<TARGET_FILE:proofrunkit>"
            "-DPRK_VERSION=${PROJECT_VERSION}"
            "-DPRK_WXS=${_prk_wxs}"
            "-DPRK_LICENSE_RTF=${_prk_license_rtf}"
            "-DPRK_LICENSE_TXT=${_prk_license_txt}"
            "-DPRK_URL_SHORTCUT=${_prk_url_shortcut}"
            "-DPRK_MSI_OUTPUT=${_prk_msi_output}"
            "-DPRK_INTERMEDIATE_DIR=${_prk_msi_intermediate}"
            -P "${_prk_build_msi_script}"
        DEPENDS
            proofrunkit
            "${_prk_wxs}"
            "${_prk_license_rtf}"
            "${_prk_license_txt}"
            "${_prk_url_shortcut}"
            "${_prk_build_msi_script}"
        COMMENT "Building ProofRunKit x64 MSI with WiX v4"
        VERBATIM)
    add_custom_target(package-msi DEPENDS "${_prk_msi_output}")
    # Expose the path so scripts and docs can print it after the build.
    set(PRK_MSI_OUTPUT "${_prk_msi_output}" PARENT_SCOPE)
endfunction()

prk_add_package_msi_target()
