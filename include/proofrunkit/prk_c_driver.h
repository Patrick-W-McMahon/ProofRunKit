/**
 * @file prk_c_driver.h
 * @brief C and C++ compiler driver (#14): builds the command line for gcc, clang, or MSVC cl (or the ar / lib
 *        archiver), runs it through a PrkProcessRunner (#13), and parses the diagnostics into structured errors
 *        and warnings. Produces executables, object files, static libraries, and shared libraries; the produced
 *        program is then run with the spawn layer.
 *
 * The driver never compiles anything itself and never uses a shell for the compiler. gcc and clang are found on
 * PATH; MSVC cl is used from PATH (a Developer Command Prompt) or, when it is not there, found with vswhere and
 * vcvarsall.bat (prk_msvc_toolchain.h). All functions except prk_compile_program are pure.
 */
#ifndef PROOFRUNKIT_PRK_C_DRIVER_H
#define PROOFRUNKIT_PRK_C_DRIVER_H

#include <stddef.h>
#include <stdint.h>

#include "proofrunkit/prk.h"
#include "proofrunkit/prk_msvc_toolchain.h"
#include "proofrunkit/prk_process.h"

#ifdef __cplusplus
extern "C" {
#endif

/** @brief Most diagnostics kept per compile; the counts still include every one. */
#define PRK_COMPILER_MAX_DIAGNOSTICS 1000

/** @brief Compiler families the driver can drive. */
typedef enum PrkCompilerFamily {
    PRK_COMPILER_AUTO = 0,  /**< Try each family in turn (Windows: cl, clang, gcc; else gcc, clang) until one starts. */
    PRK_COMPILER_GCC = 1,   /**< gcc / g++ (also MinGW-w64). */
    PRK_COMPILER_CLANG = 2, /**< clang / clang++. */
    PRK_COMPILER_MSVC = 3   /**< Microsoft cl.exe. */
} PrkCompilerFamily;

/** @brief Source language; selects the C++ driver name (g++, clang++) and MSVC exception flags. */
typedef enum PrkSourceLanguage { PRK_SOURCE_LANGUAGE_C = 0, PRK_SOURCE_LANGUAGE_CXX = 1 } PrkSourceLanguage;

/** @brief What a compile produces. */
typedef enum PrkOutputKind {
    PRK_OUTPUT_EXECUTABLE = 0,     /**< A program (compile and link). */
    PRK_OUTPUT_OBJECT = 1,         /**< One object file from one source (-c / /c): the separate compile step. */
    PRK_OUTPUT_STATIC_LIBRARY = 2, /**< An archive of object files (ar rcs / lib /OUT:); no compiler runs. */
    PRK_OUTPUT_SHARED_LIBRARY = 3  /**< A shared library (-shared / /LD). */
} PrkOutputKind;

/** @brief What to compile. Every pointer is borrowed and never modified, except the toolchain cache. */
typedef struct PrkCompileRequest {
    PrkCompilerFamily family;     /**< Which compiler; AUTO tries the families in order. */
    PrkSourceLanguage language;   /**< C or C++. */
    const char* compiler_program; /**< Optional program override, e.g. "x86_64-w64-mingw32-gcc"; needs a family. */
    PrkOutputKind kind;           /**< Executable (default), object, static library, or shared library. */
    const char* const* sources;   /**< Sources or object files (at least one; exactly one for an object). */
    size_t source_count;          /**< Entries in @p sources. */
    const char* output_path; /**< File to produce; for executables on Windows ".exe" is added without an extension. */
    const char* standard;    /**< Optional language standard, e.g. "c17" or "c++20". */
    const char* const* include_directories; /**< -I / /I directories. */
    size_t include_directory_count;         /**< Entries in @p include_directories. */
    const char* const* defines;             /**< -D / /D macros, "NAME" or "NAME=VALUE". */
    size_t define_count;                    /**< Entries in @p defines. */
    const char* const* extra_flags;         /**< Passed to the compiler unchanged, after the generated flags. */
    size_t extra_flag_count;                /**< Entries in @p extra_flags. */
    const char* const* libraries; /**< Libraries to link: "m" becomes -lm or m.lib; names with a '.' are kept. */
    size_t library_count;         /**< Entries in @p libraries. */
    const char* const* library_directories; /**< -L / /LIBPATH: directories. */
    size_t library_directory_count;         /**< Entries in @p library_directories. */
    const char* archiver_program;           /**< Optional archiver override for static libraries (default ar / lib). */
    const char* msvc_architecture;          /**< vcvarsall architecture for discovery, e.g. "x64"; NULL = native. */
    PrkMsvcToolchain* msvc_toolchain; /**< Optional cache: filled by the first discovery, then reused; may be NULL. */
    int is_warnings_as_errors;        /**< 1 adds -Werror / /WX. Warnings are always enabled (-Wall -Wextra / /W4). */
    const char* working_directory;    /**< Directory the compiler runs in; NULL inherits. */
    uint64_t timeout_ms;              /**< Compiler timeout; 0 selects the spawn layer's default. */
} PrkCompileRequest;

/** @brief Severity of one diagnostic. */
typedef enum PrkDiagnosticSeverity {
    PRK_DIAGNOSTIC_ERROR = 0,   /**< error or fatal error. */
    PRK_DIAGNOSTIC_WARNING = 1, /**< warning. */
    PRK_DIAGNOSTIC_NOTE = 2     /**< note (context for the previous diagnostic). */
} PrkDiagnosticSeverity;

/** @brief One parsed compiler diagnostic. Text fields are truncated to fit and always terminated. */
typedef struct PrkCompilerDiagnostic {
    PrkDiagnosticSeverity severity; /**< error, warning, or note. */
    char file[512];                 /**< Source path as printed, or the tool name ("collect2", "LINK"). */
    unsigned line;                  /**< 1-based line, or 0 when the diagnostic has none. */
    unsigned column;                /**< 1-based column, or 0 when not printed. */
    char code[48];                  /**< MSVC code ("C2065") or GCC/Clang option ("-Wunused-variable"), else "". */
    char message[1024];             /**< Message text without location, severity, or code. */
} PrkCompilerDiagnostic;

/** @brief Result of one compile; release with prk_release_compile_result(). */
typedef struct PrkCompileResult {
    PrkProcessResult process;           /**< The compiler run (outcome, exit code, stdout, stderr, time). */
    PrkCompilerFamily family;           /**< Family that ran (the last tried when none started). */
    PrkOutputKind kind;                 /**< What was produced. */
    int is_msvc_discovered;             /**< 1 when cl (or lib) came from vswhere/vcvarsall, not from PATH. */
    char compiler_program[256];         /**< Program that ran, e.g. "g++". */
    char* command_line;                 /**< The command for reports (malloc'd, Windows quoting); never NULL. */
    char output_path[1024];             /**< Path of the produced file (executables: with ".exe" on Windows). */
    PrkCompilerDiagnostic* diagnostics; /**< Parsed diagnostics (malloc'd), in output order; may be NULL. */
    size_t diagnostic_count;            /**< Entries in @p diagnostics. */
    size_t error_count;                 /**< Errors, including any beyond PRK_COMPILER_MAX_DIAGNOSTICS. */
    size_t warning_count;               /**< Warnings, likewise. */
} PrkCompileResult;

/**
 * @brief Compiles (and links or archives) per request->kind. Tries every family when the request says AUTO;
 *        for MSVC, falls back to vswhere/vcvarsall discovery when cl is not on PATH.
 * @param runner Process runner (the system runner, or a fake in tests).
 * @param request What to compile; not modified.
 * @param out_result Receives the result on PRK_STATUS_OK. A compiler that is missing, fails, or times out is
 *        a result (process.outcome / exit_code), never an error status.
 * @return PRK_STATUS_OK, PRK_STATUS_INVALID_ARGUMENT, or PRK_STATUS_OUT_OF_MEMORY.
 *         Side effects: runs the compiler or archiver, which writes the output and object files; may run
 *         MSVC discovery and store it in request->msvc_toolchain.
 */
PrkStatus prk_compile_program(const PrkProcessRunner* runner, const PrkCompileRequest* request,
                              PrkCompileResult* out_result);

/** @brief Frees a compile result and resets it. @param result Result to release (a documented mutator); may be NULL. */
void prk_release_compile_result(PrkCompileResult* result);

/**
 * @brief Parses one line of compiler output.
 * @param family GCC, CLANG, or MSVC (GCC and Clang share a format).
 * @param line One line, without its line break. @param out_diagnostic Receives the diagnostic when found.
 * @return 1 when the line is a diagnostic, else 0 (and @p out_diagnostic is unchanged). Pure function.
 */
int prk_parse_compiler_diagnostic(PrkCompilerFamily family, const char* line, PrkCompilerDiagnostic* out_diagnostic);

/**
 * @brief Returns the default program for a family and language: gcc/g++, clang/clang++, or cl.
 * @param family A concrete family (not AUTO). @param language C or C++.
 * @return A static string, or NULL for AUTO. Pure function.
 */
const char* prk_get_default_compiler_program(PrkCompilerFamily family, PrkSourceLanguage language);

/** @brief Returns "executable", "object", "static_library", or "shared_library". @param kind Kind. @return Static. */
const char* prk_get_output_kind_name(PrkOutputKind kind);

/** @brief Returns "auto", "gcc", "clang", or "msvc". @param family Family to name. @return A static string. Pure. */
const char* prk_get_compiler_family_name(PrkCompilerFamily family);

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* PROOFRUNKIT_PRK_C_DRIVER_H */
