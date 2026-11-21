# Project-wide warning and reproducibility flags (#7, #22).

# Applies the project's warning level (and optional /Brepro) to TARGET.
function(prk_apply_project_warnings target)
    if(MSVC)
        target_compile_options(${target} PRIVATE
            $<$<COMPILE_LANGUAGE:C,CXX>:/W4 /permissive- /utf-8>
            $<$<AND:$<COMPILE_LANGUAGE:C,CXX>,$<BOOL:${PRK_WARNINGS_AS_ERRORS}>>:/WX>
            $<$<AND:$<COMPILE_LANGUAGE:C,CXX>,$<BOOL:${PRK_REPRODUCIBLE_BUILD}>>:/Brepro>)
        target_compile_definitions(${target} PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX)
        get_target_property(target_type ${target} TYPE)
        if(PRK_REPRODUCIBLE_BUILD AND target_type STREQUAL "EXECUTABLE")
            target_link_options(${target} PRIVATE /Brepro)
        endif()
    else()
        target_compile_options(${target} PRIVATE
            $<$<COMPILE_LANGUAGE:C,CXX>:-Wall -Wextra -Wpedantic>
            $<$<AND:$<COMPILE_LANGUAGE:C,CXX>,$<BOOL:${PRK_WARNINGS_AS_ERRORS}>>:-Werror>)
    endif()
endfunction()
