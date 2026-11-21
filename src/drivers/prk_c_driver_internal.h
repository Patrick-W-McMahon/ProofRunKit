/**
 * @file prk_c_driver_internal.h
 * @brief Pure helpers of the C/C++ compiler driver (#14) exposed for unit tests. Not public API.
 */
#ifndef PROOFRUNKIT_SRC_DRIVERS_PRK_C_DRIVER_INTERNAL_H
#define PROOFRUNKIT_SRC_DRIVERS_PRK_C_DRIVER_INTERNAL_H

#include <stddef.h>

#include "proofrunkit/prk_c_driver.h"

#ifdef __cplusplus
extern "C" {
#endif

/** @brief A growing list of malloc'd argument strings. */
typedef struct PrkArgumentList {
    char** items;         /**< Arguments (malloc'd strings). */
    size_t count;         /**< Arguments in @p items. */
    size_t capacity;      /**< Slots allocated. */
    int is_out_of_memory; /**< Set when an append failed; later appends are ignored. */
} PrkArgumentList;

/**
 * @brief Builds the compiler arguments (without the program name) for one family.
 * @param request A validated request. @param family GCC, CLANG, or MSVC.
 * @param output_path Final output path (see prk_format_compile_output_path).
 * @param out_arguments Receives the list (a documented mutator); free with prk_free_argument_list().
 * @return PRK_STATUS_OK or PRK_STATUS_OUT_OF_MEMORY. Pure apart from the allocation.
 */
PrkStatus prk_build_compiler_arguments(const PrkCompileRequest* request, PrkCompilerFamily family,
                                       const char* output_path, PrkArgumentList* out_arguments);

/** @brief Frees an argument list. @param arguments List to free (a documented mutator). */
void prk_free_argument_list(PrkArgumentList* arguments);

/**
 * @brief Chooses the output path: @p requested, plus ".exe" on Windows when its file name has no extension.
 * @param requested Requested path. @param out_path Receives the path. @param capacity Bytes in @p out_path.
 * @return Nothing. Pure function.
 */
void prk_format_compile_output_path(const char* requested, char* out_path, size_t capacity);

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* PROOFRUNKIT_SRC_DRIVERS_PRK_C_DRIVER_INTERNAL_H */
