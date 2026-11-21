/**
 * @file prk_asm_driver.h
 * @brief Assembler pass (#16): assembles one source into an object file with MASM (ml64 / ml), the GNU
 *        assembler (as), or NASM through a PrkProcessRunner (#13), and parses the diagnostics. The object is then
 *        linked with the C/C++ driver (prk_compile_program, kind executable) and run with the spawn layer.
 *
 * MASM comes from PATH or, when it is not there, from the MSVC toolchain found with vswhere/vcvarsall
 * (prk_msvc_toolchain.h): ml64.exe or ml.exe next to cl.exe. AUTO picks by file extension: ".s" / ".S" use GAS,
 * ".nasm" uses NASM, and ".asm" uses MASM on Windows and NASM elsewhere. Never uses a shell.
 */
#ifndef PROOFRUNKIT_PRK_ASM_DRIVER_H
#define PROOFRUNKIT_PRK_ASM_DRIVER_H

#include <stddef.h>
#include <stdint.h>

#include "proofrunkit/prk.h"
#include "proofrunkit/prk_c_driver.h"
#include "proofrunkit/prk_msvc_toolchain.h"
#include "proofrunkit/prk_process.h"

#ifdef __cplusplus
extern "C" {
#endif

/** @brief Assemblers the pass can drive. */
typedef enum PrkAssemblerFamily {
    PRK_ASSEMBLER_AUTO = 0, /**< Pick by the source's extension (see the file comment). */
    PRK_ASSEMBLER_MASM = 1, /**< Microsoft ml64.exe (x64 and arm64 hosts targeting x64) or ml.exe (x86). */
    PRK_ASSEMBLER_GAS = 2,  /**< GNU as (also MinGW-w64's as). */
    PRK_ASSEMBLER_NASM = 3  /**< NASM. */
} PrkAssemblerFamily;

/** @brief What to assemble. Every pointer is borrowed and never modified, except the toolchain cache. */
typedef struct PrkAssembleRequest {
    PrkAssemblerFamily family;     /**< Which assembler; AUTO picks by extension. */
    const char* assembler_program; /**< Optional program override, e.g. "x86_64-w64-mingw32-as"; needs a family. */
    const char* source;            /**< The one source file. */
    const char* output_path;       /**< Object file to produce. */
    const char* const* include_directories; /**< /I, -I directories. */
    size_t include_directory_count;         /**< Entries in @p include_directories. */
    const char* const* defines;             /**< /D, -D (NASM), or --defsym NAME=VALUE (GAS; "NAME" means NAME=1). */
    size_t define_count;                    /**< Entries in @p defines. */
    const char* const* extra_flags;         /**< Passed unchanged after the generated flags. */
    size_t extra_flag_count;                /**< Entries in @p extra_flags. */
    int is_warnings_as_errors;              /**< 1 adds /WX (MASM), --fatal-warnings (GAS), or -Werror (NASM). */
    const char* object_format;     /**< NASM -f format; NULL = win64/win32, macho64, or elf64/elf32 for this build. */
    const char* msvc_architecture; /**< vcvarsall architecture; x86 targets use ml.exe. NULL = native. */
    PrkMsvcToolchain* msvc_toolchain; /**< Optional discovery cache; may be NULL. */
    const char* working_directory;    /**< NULL inherits. */
    uint64_t timeout_ms;              /**< 0 selects the spawn layer's default. */
} PrkAssembleRequest;

/**
 * @brief Assembles request->source into request->output_path.
 * @param runner Process runner. @param request What to assemble; not modified.
 * @param out_result Receives the result on PRK_STATUS_OK (kind = PRK_OUTPUT_OBJECT; family = MSVC for MASM, else
 *        GCC, naming the diagnostics format). A missing assembler is a result (outcome "failed to start").
 * @return PRK_STATUS_OK, PRK_STATUS_INVALID_ARGUMENT, or PRK_STATUS_OUT_OF_MEMORY.
 *         Side effects: runs the assembler (and MSVC discovery for MASM when ml64 is not on PATH).
 */
PrkStatus prk_assemble_source(const PrkProcessRunner* runner, const PrkAssembleRequest* request,
                              PrkCompileResult* out_result);

/**
 * @brief Resolves AUTO for @p source: GAS for ".s"/".S", NASM for ".nasm", MASM (Windows) or NASM for ".asm".
 * @param family Requested family. @param source Source path.
 * @return A concrete family, or PRK_ASSEMBLER_AUTO when the extension is unknown. Pure function.
 */
PrkAssemblerFamily prk_choose_assembler_family(PrkAssemblerFamily family, const char* source);

/** @brief Returns "auto", "masm", "gas", or "nasm". @param family Family. @return A static string. Pure. */
const char* prk_get_assembler_family_name(PrkAssemblerFamily family);

/** @brief Returns NASM's default -f format for this build ("win64", "win32", "macho64", "elf64", "elf32"). Pure. */
const char* prk_get_default_nasm_format(void);

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* PROOFRUNKIT_PRK_ASM_DRIVER_H */
