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

/** @brief Appends the concatenation of @p prefix and @p value. */
static void append_argument(PrkArgumentList* arguments, const char* prefix, const char* value) {
    if (arguments->is_out_of_memory) {
        return;
    }
    if (arguments->count == arguments->capacity) {
        const size_t capacity = arguments->capacity == 0 ? 16 : arguments->capacity * 2;
        char** const grown = (char**)realloc(arguments->items, capacity * sizeof *grown);
        if (grown == NULL) {
            arguments->is_out_of_memory = 1;
            return;
        }
        arguments->items = grown;
        arguments->capacity = capacity;
    }
    const size_t prefix_length = strlen(prefix);
    const size_t value_length = strlen(value);
    char* const text = (char*)malloc(prefix_length + value_length + 1);
    if (text == NULL) {
        arguments->is_out_of_memory = 1;
        return;
    }
    memcpy(text, prefix, prefix_length);
    memcpy(text + prefix_length, value, value_length + 1);
    arguments->items[arguments->count++] = text;
}

void prk_free_argument_list(PrkArgumentList* arguments) {
    for (size_t index = 0; index < arguments->count; ++index) {
        free(arguments->items[index]);
    }
    free(arguments->items);
    memset(arguments, 0, sizeof *arguments);
}

/** @brief Appends each entry of @p values with @p prefix. */
static void append_each(PrkArgumentList* arguments, const char* prefix, const char* const* values, size_t count) {
    for (size_t index = 0; index < count; ++index) {
        append_argument(arguments, prefix, values[index]);
    }
}

PrkStatus prk_build_compiler_arguments(const PrkCompileRequest* request, PrkCompilerFamily family,
                                       const char* output_path, PrkArgumentList* out_arguments) {
    memset(out_arguments, 0, sizeof *out_arguments);
    const int has_standard = request->standard != NULL && request->standard[0] != '\0';
    if (family == PRK_COMPILER_MSVC) {
        append_argument(out_arguments, "/nologo", "");
        append_argument(out_arguments, "/W4", "");
        if (request->is_warnings_as_errors) {
            append_argument(out_arguments, "/WX", "");
        }
        if (request->language == PRK_SOURCE_LANGUAGE_CXX) {
            append_argument(out_arguments, "/EHsc", "");
        }
        if (has_standard) {
            append_argument(out_arguments, "/std:", request->standard);
        }
        append_each(out_arguments, "/I", request->include_directories, request->include_directory_count);
        append_each(out_arguments, "/D", request->defines, request->define_count);
        append_each(out_arguments, "", request->sources, request->source_count);
        append_argument(out_arguments, "/Fe", output_path);
    } else {
        append_argument(out_arguments, "-fdiagnostics-color=never", "");
        append_argument(out_arguments, "-Wall", "");
        append_argument(out_arguments, "-Wextra", "");
        if (request->is_warnings_as_errors) {
            append_argument(out_arguments, "-Werror", "");
        }
        if (has_standard) {
            append_argument(out_arguments, "-std=", request->standard);
        }
        append_each(out_arguments, "-I", request->include_directories, request->include_directory_count);
        append_each(out_arguments, "-D", request->defines, request->define_count);
        append_each(out_arguments, "", request->sources, request->source_count);
        append_argument(out_arguments, "-o", "");
        append_argument(out_arguments, "", output_path);
    }
    append_each(out_arguments, "", request->extra_flags, request->extra_flag_count);
    if (out_arguments->is_out_of_memory) {
        prk_free_argument_list(out_arguments);
        return PRK_STATUS_OUT_OF_MEMORY;
    }
    return PRK_STATUS_OK;
}

void prk_format_compile_output_path(const char* requested, char* out_path, size_t capacity) {
    copy_text(requested, strlen(requested), out_path, capacity);
#if defined(_WIN32)
    const char* name = requested;
    for (const char* cursor = requested; *cursor != '\0'; ++cursor) {
        if (*cursor == '/' || *cursor == '\\') {
            name = cursor + 1;
        }
    }
    const size_t length = strlen(out_path);
    if (strchr(name, '.') == NULL && length + 5 <= capacity) {
        memcpy(out_path + length, ".exe", 5);
    }
#endif
}

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
        (request->compiler_program != NULL && request->family == PRK_COMPILER_AUTO) ||
        (request->include_directory_count != 0 && request->include_directories == NULL) ||
        (request->define_count != 0 && request->defines == NULL) ||
        (request->extra_flag_count != 0 && request->extra_flags == NULL)) {
        return PRK_STATUS_INVALID_ARGUMENT;
    }
    return PRK_STATUS_OK;
}

/** @brief Runs one family's compiler and fills @p out_result. */
static PrkStatus compile_with_family(const PrkProcessRunner* runner, const PrkCompileRequest* request,
                                     PrkCompilerFamily family, PrkCompileResult* out_result) {
    memset(out_result, 0, sizeof *out_result);
    out_result->family = family;
    const char* const program = request->compiler_program != NULL && request->compiler_program[0] != '\0'
                                    ? request->compiler_program
                                    : prk_get_default_compiler_program(family, request->language);
    copy_text(program, strlen(program), out_result->compiler_program, sizeof out_result->compiler_program);
    prk_format_compile_output_path(request->output_path, out_result->output_path, sizeof out_result->output_path);
    PrkArgumentList arguments;
    PrkStatus status = prk_build_compiler_arguments(request, family, out_result->output_path, &arguments);
    if (status != PRK_STATUS_OK) {
        return status;
    }
    status = prk_format_windows_command_line(program, (const char* const*)arguments.items, arguments.count,
                                             &out_result->command_line);
    PrkProcessRequest process_request;
    memset(&process_request, 0, sizeof process_request);
    process_request.executable = program;
    process_request.arguments = (const char* const*)arguments.items;
    process_request.argument_count = arguments.count;
    process_request.working_directory = request->working_directory;
    process_request.is_environment_inherited = 1;
    process_request.timeout_ms = request->timeout_ms;
    if (status == PRK_STATUS_OK) {
        status = runner->spawn_process(runner->runner_state, &process_request, &out_result->process);
    }
    prk_free_argument_list(&arguments);
    if (status == PRK_STATUS_OK && (collect_diagnostics(family, out_result->process.standard_output,
                                                        out_result->process.standard_output_length, out_result) != 0 ||
                                    collect_diagnostics(family, out_result->process.standard_error,
                                                        out_result->process.standard_error_length, out_result) != 0)) {
        status = PRK_STATUS_OUT_OF_MEMORY;
    }
    if (status != PRK_STATUS_OK) {
        prk_release_compile_result(out_result);
    }
    return status;
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
    static const char auto_order_text[] = "cl, clang, gcc";
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
                           "no C/C++ compiler found on PATH (tried %s)", auto_order_text);
            return status;
        }
        prk_release_compile_result(out_result);
    }
    return PRK_STATUS_INVALID_ARGUMENT; /* Not reached: the loop returns on its last family. */
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
