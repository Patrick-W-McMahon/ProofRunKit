# Embeds a VERSIONINFO resource and an application manifest in Windows executables (#22 (d)).
# A stable, well-described binary with no packer and no self-extraction is less likely to be
# flagged by antivirus heuristics than an anonymous executable.

set(_prk_windows_dir "${CMAKE_CURRENT_LIST_DIR}/../src/windows")

# Adds the generated .rc (VERSIONINFO + manifest) to TARGET when building for Windows.
function(prk_add_windows_resources target)
    if(NOT WIN32)
        return()
    endif()
    set(PRK_MANIFEST_PATH "${_prk_windows_dir}/proofrunkit.manifest")
    file(TO_CMAKE_PATH "${PRK_MANIFEST_PATH}" PRK_MANIFEST_PATH)
    set(PRK_FILE_VERSION_COMMAS "${PROJECT_VERSION_MAJOR},${PROJECT_VERSION_MINOR},${PROJECT_VERSION_PATCH},0")
    set(PRK_ORIGINAL_FILENAME "${target}.exe")
    configure_file("${_prk_windows_dir}/proofrunkit.rc.in" "${CMAKE_CURRENT_BINARY_DIR}/${target}.rc" @ONLY)
    target_sources(${target} PRIVATE "${CMAKE_CURRENT_BINARY_DIR}/${target}.rc")
    if(MSVC)
        # The manifest is embedded by the .rc file; stop the linker from adding a second one.
        target_link_options(${target} PRIVATE /MANIFEST:NO)
    endif()
endfunction()
