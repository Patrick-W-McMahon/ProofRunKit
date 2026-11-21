/**
 * @file prk_version.c
 * @brief Version string, injected by CMake from project(VERSION ...).
 */
#include "proofrunkit/prk.h"

#ifndef PRK_VERSION_STRING
#error "PRK_VERSION_STRING must be defined by the build (see CMakeLists.txt)."
#endif

const char* prk_get_version_string(void) {
    return PRK_VERSION_STRING;
}
