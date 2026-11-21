# Defines the package-msi target for the Windows WiX v4 installer (#37, #38, #40, #58).
#
# Direct `wix build` is used instead of CPack's WIX generator so the UpgradeCode,
# Program Files destination, optional PATH feature, Start Menu entries, and ARP
# properties stay in first-party authoring (installer/windows/ProofRunKit-x64.wxs
# or ProofRunKit-x86.wxs). The architecture of the current toolchain selects
# which package is built: an x64 (or other 64-bit) MSVC tree produces
# ProofRunKit-<version>-win-x64.msi; a Win32 / 32-bit tree produces
# ProofRunKit-<version>-win-x86.msi.

set(_prk_installer_dir "${CMAKE_CURRENT_LIST_DIR}/../installer/windows")
set(_prk_msi_dir "${CMAKE_BINARY_DIR}/installer")
set(_prk_msi_intermediate "${_prk_msi_dir}/wixobj")
# Single license source (#58): the MSI packages the repo-root LICENSE (as LICENSE.txt) and NOTICE
# (as NOTICE.txt, Apache-2.0 section 4(d)), and the license dialog RTF is generated from LICENSE.
set(_prk_license_txt "${PROJECT_SOURCE_DIR}/LICENSE")
set(_prk_notice_txt "${PROJECT_SOURCE_DIR}/NOTICE")
set(_prk_license_rtf "${_prk_msi_dir}/License.rtf")
set(_prk_url_shortcut "${_prk_installer_dir}/ProofRunKit.url")
include(ProofRunKitLicenseRtf)
file(MAKE_DIRECTORY "${_prk_msi_dir}")
prk_write_license_rtf("${_prk_license_txt}" "${_prk_license_rtf}")
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${_prk_license_txt}")

if(CMAKE_GENERATOR_PLATFORM STREQUAL "Win32" OR CMAKE_SIZEOF_VOID_P EQUAL 4)
    set(_prk_msi_arch "x86")
    set(_prk_wxs "${_prk_installer_dir}/ProofRunKit-x86.wxs")
    set(_prk_msi_output "${_prk_msi_dir}/ProofRunKit-${PROJECT_VERSION}-win-x86.msi")
    set(_prk_msi_comment "Building ProofRunKit x86 MSI with WiX v4")
else()
    set(_prk_msi_arch "x64")
    set(_prk_wxs "${_prk_installer_dir}/ProofRunKit-x64.wxs")
    set(_prk_msi_output "${_prk_msi_dir}/ProofRunKit-${PROJECT_VERSION}-win-x64.msi")
    set(_prk_msi_comment "Building ProofRunKit x64 MSI with WiX v4")
endif()

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
    message(STATUS "ProofRunKit MSI (${_prk_msi_arch}): WiX tool at ${PRK_WIX_EXECUTABLE}")
else()
    message(STATUS "ProofRunKit MSI (${_prk_msi_arch}): WiX tool not found at configure time; package-msi will search again at build time")
endif()

# Builds the architecture-matching MSI from the current proofrunkit executable
# using WiX v4. Not part of ALL.
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
            "-DPRK_MSI_ARCH=${_prk_msi_arch}"
            "-DPRK_LICENSE_RTF=${_prk_license_rtf}"
            "-DPRK_LICENSE_TXT=${_prk_license_txt}"
            "-DPRK_NOTICE_TXT=${_prk_notice_txt}"
            "-DPRK_URL_SHORTCUT=${_prk_url_shortcut}"
            "-DPRK_MSI_OUTPUT=${_prk_msi_output}"
            "-DPRK_INTERMEDIATE_DIR=${_prk_msi_intermediate}"
            -P "${_prk_build_msi_script}"
        DEPENDS
            proofrunkit
            "${_prk_wxs}"
            "${_prk_license_rtf}"
            "${_prk_license_txt}"
            "${_prk_notice_txt}"
            "${_prk_url_shortcut}"
            "${_prk_build_msi_script}"
        COMMENT "${_prk_msi_comment}"
        VERBATIM)
    add_custom_target(package-msi DEPENDS "${_prk_msi_output}")
    # Expose the path so scripts and docs can print it after the build.
    set(PRK_MSI_OUTPUT "${_prk_msi_output}" PARENT_SCOPE)
endfunction()

prk_add_package_msi_target()
