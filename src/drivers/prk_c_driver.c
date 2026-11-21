/**
 * @file prk_c_driver.c
 * @brief C and C++ compiler driver (#14, first slice): command lines for gcc, clang, and MSVC cl,
 *        diagnostics collection (parsers in prk_c_diagnostics.c), and the compile itself through an
 *        injected PrkProcessRunner (#13).
 *
 * Must not touch the file system or environment itself: the only side effect is the compiler process the
 * runner starts. MSVC discovery through vswhere/vcvars is a later slice; cl must be on PATH for now.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "prk_c_driver_internal.h"
#include "process/prk_process_internal.h"

const char* prk_get_compiler_family_name(PrkCompilerFamily family) {
    switch (family) {
        case PRK_COMPILER_AUTO:
            return "auto";
        case PRK_COMPILER_GCC:
            return "gcc";
        case PRK_COMPILER_CLANG:
            return "clang";
        case PRK_COMPILER_MSVC:
            return "msvc";
    }
    return "unknown";
}

const char* prk_get_default_compiler_program(PrkCompilerFamily family, PrkSourceLanguage language) {
    const int is_cxx = language == PRK_SOURCE_LANGUAGE_CXX;
    switch (family) {
        case PRK_COMPILER_GCC:
            return is_cxx ? "g++" : "gcc";
        case PRK_COMPILER_CLANG:
            return is_cxx ? "clang++" : "clang";
        case PRK_COMPILER_MSVC:
            return "cl";
        case PRK_COMPILER_AUTO:
            break;
    }
    return NULL;
}

/** @brief Copies at most @p length bytes of @p text into a terminated buffer. */
static void copy_text(const char* text, size_t length, char* out_text, size_t capacity) {
    const size_t copied = length < capacity - 1 ? length : capacity - 1;
    memcpy(out_text, text, copied);
    out_text[copied] = '\0';
}

/* ------------------------------------------------------------------ command lines */

/* ------------------------------------------------------------------ diagnostics (parsers: prk_c_diagnostics.c) */

/** @brief Parses every line of @p text into @p result. @return 0 on success, -1 when out of memory. */
static int collect_diagnostics(PrkCompilerFamily family, const char* text, size_t length, PrkCompileResult* result) {
    char line[2048];
    size_t start = 0;
    while (start < length) {
        size_t end = start;
        while (end < length && text[end] != '\n') {
            ++end;
        }
        size_t line_length = end - start;
        if (line_length > 0 && text[start + line_length - 1] == '\r') {
            --line_length;
        }
        copy_text(text + start, line_length, line, sizeof line);
        PrkCompilerDiagnostic diagnostic;
        if (prk_parse_compiler_diagnostic(family, line, &diagnostic)) {
            result->error_count += diagnostic.severity == PRK_DIAGNOSTIC_ERROR ? 1u : 0u;
            result->warning_count += diagnostic.severity == PRK_DIAGNOSTIC_WARNING ? 1u : 0u;
            if (result->diagnostic_count < PRK_COMPILER_MAX_DIAGNOSTICS) {
                PrkCompilerDiagnostic* const grown = (PrkCompilerDiagnostic*)realloc(
                    result->diagnostics, (result->diagnostic_count + 1) * sizeof *grown);
                if (grown == NULL) {
                    return -1;
                }
                result->diagnostics = grown;
                result->diagnostics[result->diagnostic_count++] = diagnostic;
            }
        }
        start = end + 1;
    }
    return 0;
}

/* ------------------------------------------------------------------ compile */

static PrkStatus check_compile_request(const PrkProcessRunner* runner, const PrkCompileRequest* request) {
    if (runner == NULL || runner->spawn_process == NULL || request == NULL || request->sources == NULL ||
        request->source_count == 0 || request->output_path == NULL || request->output_path[0] == '\0' ||
        (int)request->kind < 0 || request->kind > PRK_OUTPUT_SHARED_LIBRARY ||
        (request->kind == PRK_OUTPUT_OBJECT && request->source_count != 1) ||
        (request->compiler_program != NULL && request->family == PRK_COMPILER_AUTO) ||
        (request->archiver_program != NULL && request->family == PRK_COMPILER_AUTO) ||
        (request->msvc_architecture != NULL && !prk_is_msvc_architecture_allowed(request->msvc_architecture)) ||
        (request->include_directory_count != 0 && request->include_directories == NULL) ||
        (request->define_count != 0 && request->defines == NULL) ||
        (request->extra_flag_count != 0 && request->extra_flags == NULL) ||
        (request->library_count != 0 && request->libraries == NULL) ||
        (request->library_directory_count != 0 && request->library_directories == NULL)) {
        return PRK_STATUS_INVALID_ARGUMENT;
    }
    return PRK_STATUS_OK;
}

/** @brief Returns @p text when it is a non-empty string, else NULL. */
static const char* get_non_empty(const char* text) {
    return text != NULL && text[0] != '\0' ? text : NULL;
}

/** @brief Chooses the program: an override, the discovered MSVC tool, or the family default. */
static const char* choose_program(const PrkCompileRequest* request, PrkCompilerFamily family,
                                  const PrkMsvcToolchain* toolchain) {
    if (request->kind == PRK_OUTPUT_STATIC_LIBRARY) {
        if (get_non_empty(request->archiver_program) != NULL) {
            return request->archiver_program;
        }
        return toolchain != NULL ? toolchain->lib_path : family == PRK_COMPILER_MSVC ? "lib" : "ar";
    }
    if (get_non_empty(request->compiler_program) != NULL) {
        return request->compiler_program;
    }
    return toolchain != NULL ? toolchain->cl_path : prk_get_default_compiler_program(family, request->language);
}

PrkStatus prk_run_driver_tool(const PrkProcessRunner* runner, const PrkDriverTool* tool, PrkCompileResult* out_result) {
    copy_text(tool->program, strlen(tool->program), out_result->compiler_program, sizeof out_result->compiler_program);
    const char* const* const arguments = (const char* const*)tool->arguments->items;
    PrkStatus status =
        prk_format_windows_command_line(tool->program, arguments, tool->arguments->count, &out_result->command_line);
    PrkProcessRequest process_request;
    memset(&process_request, 0, sizeof process_request);
    process_request.executable = tool->program;
    process_request.arguments = arguments;
    process_request.argument_count = tool->arguments->count;
    process_request.working_directory = tool->working_directory;
    process_request.environment = tool->toolchain != NULL ? (const char* const*)tool->toolchain->environment : NULL;
    process_request.environment_count = tool->toolchain != NULL ? tool->toolchain->environment_count : 0;
    process_request.is_environment_inherited = tool->toolchain == NULL;
    process_request.timeout_ms = tool->timeout_ms;
    if (status == PRK_STATUS_OK) {
        status = runner->spawn_process(runner->runner_state, &process_request, &out_result->process);
    }
    const PrkProcessResult* const process = &out_result->process;
    if (status == PRK_STATUS_OK && (collect_diagnostics(tool->diagnostics_family, process->standard_output,
                                                        process->standard_output_length, out_result) != 0 ||
                                    collect_diagnostics(tool->diagnostics_family, process->standard_error,
                                                        process->standard_error_length, out_result) != 0)) {
        status = PRK_STATUS_OUT_OF_MEMORY;
    }
    if (status != PRK_STATUS_OK) {
        prk_release_compile_result(out_result);
    }
    return status;
}

/** @brief Runs one tool (compiler or archiver) and fills @p out_result. @p toolchain is NULL to use PATH. */
static PrkStatus run_tool(const PrkProcessRunner* runner, const PrkCompileRequest* request, PrkCompilerFamily family,
                          const PrkMsvcToolchain* toolchain, PrkCompileResult* out_result) {
    memset(out_result, 0, sizeof *out_result);
    out_result->family = family;
    out_result->kind = request->kind;
    out_result->is_msvc_discovered = toolchain != NULL;
    prk_format_compile_output_path(request->output_path, request->kind, out_result->output_path,
                                   sizeof out_result->output_path);
    PrkArgumentList arguments;
    PrkStatus status = prk_build_compiler_arguments(request, family, out_result->output_path, &arguments);
    if (status != PRK_STATUS_OK) {
        return status;
    }
    const PrkDriverTool tool = {choose_program(request, family, toolchain),
                                &arguments,
                                request->working_directory,
                                toolchain,
                                request->timeout_ms,
                                family};
    status = prk_run_driver_tool(runner, &tool, out_result);
    prk_free_argument_list(&arguments);
    return status;
}

/** @brief Retries a cl / lib that is not on PATH with the toolchain from vswhere/vcvarsall (cached). */
static PrkStatus retry_with_discovered_msvc(const PrkProcessRunner* runner, const PrkCompileRequest* request,
                                            PrkMsvcToolchain* toolchain, PrkCompileResult* out_result) {
    if (!toolchain->is_attempted) {
        const PrkStatus status = prk_discover_msvc_toolchain(runner, request->msvc_architecture, toolchain);
        if (status != PRK_STATUS_OK) {
            prk_release_compile_result(out_result);
            return status;
        }
    }
    if (toolchain->is_found) {
        prk_release_compile_result(out_result);
        return run_tool(runner, request, PRK_COMPILER_MSVC, toolchain, out_result);
    }
    (void)snprintf(out_result->process.error_message, sizeof out_result->process.error_message,
                   "%.60s is not on PATH and MSVC discovery failed: %.140s", out_result->compiler_program,
                   toolchain->error_message);
    return PRK_STATUS_OK;
}

/** @brief MSVC: a cached toolchain, else cl / lib from PATH, else discovery. */
static PrkStatus compile_with_msvc(const PrkProcessRunner* runner, const PrkCompileRequest* request,
                                   PrkCompileResult* out_result) {
    PrkMsvcToolchain local_toolchain;
    memset(&local_toolchain, 0, sizeof local_toolchain);
    PrkMsvcToolchain* const toolchain = request->msvc_toolchain != NULL ? request->msvc_toolchain : &local_toolchain;
    const int has_override = request->kind == PRK_OUTPUT_STATIC_LIBRARY
                                 ? get_non_empty(request->archiver_program) != NULL
                                 : get_non_empty(request->compiler_program) != NULL;
    PrkStatus status;
    if (toolchain->is_found && !has_override) {
        status = run_tool(runner, request, PRK_COMPILER_MSVC, toolchain, out_result);
    } else {
        status = run_tool(runner, request, PRK_COMPILER_MSVC, NULL, out_result);
        if (status == PRK_STATUS_OK && out_result->process.outcome == PRK_PROCESS_FAILED_TO_START && !has_override) {
            status = retry_with_discovered_msvc(runner, request, toolchain, out_result);
        }
    }
    prk_release_msvc_toolchain(&local_toolchain);
    return status;
}

/** @brief Runs one concrete family. */
static PrkStatus compile_with_family(const PrkProcessRunner* runner, const PrkCompileRequest* request,
                                     PrkCompilerFamily family, PrkCompileResult* out_result) {
    return family == PRK_COMPILER_MSVC ? compile_with_msvc(runner, request, out_result)
                                       : run_tool(runner, request, family, NULL, out_result);
}

PrkStatus prk_compile_program(const PrkProcessRunner* runner, const PrkCompileRequest* request,
                              PrkCompileResult* out_result) {
    if (out_result == NULL || check_compile_request(runner, request) != PRK_STATUS_OK) {
        return PRK_STATUS_INVALID_ARGUMENT;
    }
    if (request->family != PRK_COMPILER_AUTO) {
        return compile_with_family(runner, request, request->family, out_result);
    }
#if defined(_WIN32)
    static const PrkCompilerFamily order[] = {PRK_COMPILER_MSVC, PRK_COMPILER_CLANG, PRK_COMPILER_GCC};
    static const char auto_order_text[] = "MSVC on PATH or with vswhere, clang, gcc";
#else
    static const PrkCompilerFamily order[] = {PRK_COMPILER_GCC, PRK_COMPILER_CLANG};
    static const char auto_order_text[] = "gcc, clang";
#endif
    const size_t family_count = sizeof order / sizeof order[0];
    for (size_t index = 0; index < family_count; ++index) {
        const PrkStatus status = compile_with_family(runner, request, order[index], out_result);
        if (status != PRK_STATUS_OK || out_result->process.outcome != PRK_PROCESS_FAILED_TO_START) {
            return status;
        }
        if (index + 1 == family_count) {
            (void)snprintf(out_result->process.error_message, sizeof out_result->process.error_message,
                           "no C/C++ %s found (tried %s)",
                           request->kind == PRK_OUTPUT_STATIC_LIBRARY ? "archiver" : "compiler", auto_order_text);
            return status;
        }
        prk_release_compile_result(out_result);
    }
    return PRK_STATUS_INVALID_ARGUMENT; /* Not reached: the loop returns on its last family. */
}

const char* prk_get_output_kind_name(PrkOutputKind kind) {
    switch (kind) {
        case PRK_OUTPUT_EXECUTABLE:
            return "executable";
        case PRK_OUTPUT_OBJECT:
            return "object";
        case PRK_OUTPUT_STATIC_LIBRARY:
            return "static_library";
        case PRK_OUTPUT_SHARED_LIBRARY:
            return "shared_library";
    }
    return "unknown";
}

void prk_release_compile_result(PrkCompileResult* result) {
    if (result == NULL) {
        return;
    }
    prk_release_process_result(&result->process);
    free(result->command_line);
    free(result->diagnostics);
    memset(result, 0, sizeof *result);
}
