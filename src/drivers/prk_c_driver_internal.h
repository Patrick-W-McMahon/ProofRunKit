/**
 * @file prk_c_driver_internal.h
 * @brief Pure helpers of the C/C++ compiler driver (#14) exposed for unit tests. Not public API.
 */
#ifndef PROOFRUNKIT_SRC_DRIVERS_PRK_C_DRIVER_INTERNAL_H
#define PROOFRUNKIT_SRC_DRIVERS_PRK_C_DRIVER_INTERNAL_H

#include <stddef.h>

#include "proofrunkit/prk_asm_driver.h"
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
 * @brief Builds the cmd.exe arguments that run vcvarsall.bat and print the cl.exe path and the environment.
 * @param installation_path Visual Studio folder (validated by prk_parse_vswhere_output).
 * @param architecture An allowed vcvarsall architecture. @param out_arguments Receives the list (free it).
 * @return PRK_STATUS_OK or PRK_STATUS_OUT_OF_MEMORY. Pure apart from the allocation.
 */
PrkStatus prk_build_vcvars_arguments(const char* installation_path, const char* architecture,
                                     PrkArgumentList* out_arguments);

/**
 * @brief Parses the vcvars output (marker lines, `where cl.exe`, then `set`) into @p out_toolchain:
 *        cl_path, lib_path, environment, and is_found (error_message when cl.exe is missing).
 * @param text cmd.exe stdout. @param length Bytes in @p text. @param out_toolchain Toolchain to fill (a mutator).
 * @return PRK_STATUS_OK or PRK_STATUS_OUT_OF_MEMORY. Pure apart from the allocation.
 */
PrkStatus prk_parse_vcvars_output(const char* text, size_t length, PrkMsvcToolchain* out_toolchain);

/** @brief One tool run of a driver (compiler, archiver, or assembler). Every pointer is borrowed. */
typedef struct PrkDriverTool {
    const char* program;                  /**< Program name or path. */
    const PrkArgumentList* arguments;     /**< Arguments without the program. */
    const char* working_directory;        /**< NULL inherits. */
    const PrkMsvcToolchain* toolchain;    /**< Discovered MSVC environment, or NULL to inherit ProofRunKit's. */
    uint64_t timeout_ms;                  /**< 0 selects the spawn layer's default. */
    PrkCompilerFamily diagnostics_family; /**< MSVC format, or GCC format (also GAS and NASM). */
} PrkDriverTool;

/**
 * @brief Runs @p tool and fills compiler_program, command_line, process, and diagnostics of @p out_result (the
 *        caller sets family, kind, and output_path first). On an error status the result is released.
 * @param runner Process runner. @param tool What to run. @param out_result Result to fill (a documented mutator).
 * @return PRK_STATUS_OK (whatever the tool's outcome), PRK_STATUS_INVALID_ARGUMENT, or PRK_STATUS_OUT_OF_MEMORY.
 *         Side effects: runs the tool.
 */
PrkStatus prk_run_driver_tool(const PrkProcessRunner* runner, const PrkDriverTool* tool, PrkCompileResult* out_result);

/**
 * @brief Builds the assembler arguments (without the program name) for MASM, GAS, or NASM (#16).
 * @param request A validated request. @param family A concrete family.
 * @param out_arguments Receives the list (a documented mutator); free with prk_free_argument_list().
 * @return PRK_STATUS_OK or PRK_STATUS_OUT_OF_MEMORY. Pure apart from the allocation.
 */
PrkStatus prk_build_assembler_arguments(const PrkAssembleRequest* request, PrkAssemblerFamily family,
                                        PrkArgumentList* out_arguments);

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* PROOFRUNKIT_SRC_DRIVERS_PRK_C_DRIVER_INTERNAL_H */
