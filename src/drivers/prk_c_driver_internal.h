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
 * @brief Builds the compiler arguments (without the program name) for one family; for a static library, the
 *        archiver arguments (ar rcs / lib /OUT:).
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
 * @brief Chooses the output path: @p requested, plus ".exe" for executables on Windows when the file name has
 *        no extension.
 * @param requested Requested path. @param kind Output kind. @param out_path Receives the path.
 * @param capacity Bytes in @p out_path (at least 1).
 * @return Nothing. Pure function.
 */
void prk_format_compile_output_path(const char* requested, PrkOutputKind kind, char* out_path, size_t capacity);

/**
 * @brief Appends the concatenation of @p prefix and @p value to @p arguments (a documented mutator).
 *        On allocation failure sets is_out_of_memory; later appends are ignored. Side effects: allocation only.
 * @param arguments List to append to. @param prefix Text before @p value. @param value Text after @p prefix.
 */
void prk_append_argument(PrkArgumentList* arguments, const char* prefix, const char* value);

/**
 * @brief Reads the installation path from vswhere's output: the first non-empty line, trimmed.
 * @param text vswhere stdout. @param length Bytes in @p text. @param out_path Receives the path.
 * @param capacity Bytes in @p out_path.
 * @return 1 when a usable path was found (no '"', '%', or truncation), else 0. Pure function.
 */
int prk_parse_vswhere_output(const char* text, size_t length, char* out_path, size_t capacity);

/**
 * @brief Formats the vswhere.exe path: "<program_files_x86>\Microsoft Visual Studio\Installer\vswhere.exe".
 * @param program_files_x86 Value of %ProgramFiles(x86)%; NULL or "" selects "C:\Program Files (x86)".
 * @param out_path Receives the path. @param capacity Bytes in @p out_path (at least 1).
 * @return Nothing. Pure function.
 */
void prk_build_vswhere_path(const char* program_files_x86, char* out_path, size_t capacity);

/**
 * @brief Converts UTF-16LE tool output to UTF-8 when it is UTF-16LE (the second byte is 0); lone surrogates
 *        become U+FFFD. Otherwise sets *out_text to NULL and the caller keeps @p text as it is.
 * @param text Output bytes. @param length Bytes in @p text.
 * @param out_text Receives the malloc'd, terminated UTF-8 text or NULL. @param out_length Receives its length.
 * @return PRK_STATUS_OK or PRK_STATUS_OUT_OF_MEMORY. Pure apart from the allocation.
 */
PrkStatus prk_decode_utf16le_if_wide(const char* text, size_t length, char** out_text, size_t* out_length);

/**
 * @brief Builds the cmd.exe arguments that run vcvarsall.bat and list the environment:
 *        /d /u /c call "<installation>\VC\Auxiliary\Build\vcvarsall.bat" <arch> >nul && set
 * @param installation_path Visual Studio folder (validated by prk_parse_vswhere_output).
 * @param architecture An allowed vcvarsall architecture. @param out_arguments Receives the list (free it).
 * @return PRK_STATUS_OK or PRK_STATUS_OUT_OF_MEMORY. Pure apart from the allocation.
 */
PrkStatus prk_build_vcvars_arguments(const char* installation_path, const char* architecture,
                                     PrkArgumentList* out_arguments);

/**
 * @brief Parses the `set` listing after vcvarsall (UTF-16LE or 8-bit) into @p out_toolchain: environment, then
 *        cl_path = %VCToolsInstallDir%bin\Host%VSCMD_ARG_HOST_ARCH%\%VSCMD_ARG_TGT_ARCH%\cl.exe and lib.exe beside
 *        it, and is_found (error_message when those variables are missing).
 * @param text cmd.exe stdout. @param length Bytes in @p text. @param out_toolchain Toolchain to fill (a mutator).
 * @return PRK_STATUS_OK or PRK_STATUS_OUT_OF_MEMORY. Pure apart from the allocation.
 */
PrkStatus prk_parse_vcvars_output(const char* text, size_t length, PrkMsvcToolchain* out_toolchain);

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* PROOFRUNKIT_SRC_DRIVERS_PRK_C_DRIVER_INTERNAL_H */
