/**
 * @file prk_c_arguments.c
 * @brief Command lines of the C/C++ driver (#14): gcc / clang, MSVC cl, and the ar / lib archivers, for
 *        executables, object files, static libraries, and shared libraries; plus output-path rules.
 *
 * Pure apart from allocation; no I/O.
 */
#include <stdlib.h>
#include <string.h>

#include "prk_c_driver_internal.h"

void prk_append_argument(PrkArgumentList* arguments, const char* prefix, const char* value) {
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
        prk_append_argument(arguments, prefix, values[index]);
    }
}

/** @brief Appends each library: "m" becomes -lm or m.lib; a name with a '.' (a path or file) is kept. */
static void append_libraries(PrkArgumentList* arguments, const PrkCompileRequest* request, int is_msvc) {
    for (size_t index = 0; index < request->library_count; ++index) {
        const char* const library = request->libraries[index];
        const int is_file = strchr(library, '.') != NULL;
        if (is_msvc) {
            prk_append_argument(arguments, library, is_file ? "" : ".lib");
        } else {
            prk_append_argument(arguments, is_file ? "" : "-l", library);
        }
    }
}

/** @brief Appends the MSVC cl arguments. */
static void append_msvc_arguments(PrkArgumentList* arguments, const PrkCompileRequest* request,
                                  const char* output_path) {
    prk_append_argument(arguments, "/nologo", "");
    prk_append_argument(arguments, "/W4", "");
    if (request->is_warnings_as_errors) {
        prk_append_argument(arguments, "/WX", "");
    }
    if (request->language == PRK_SOURCE_LANGUAGE_CXX) {
        prk_append_argument(arguments, "/EHsc", "");
    }
    if (request->standard != NULL && request->standard[0] != '\0') {
        prk_append_argument(arguments, "/std:", request->standard);
    }
    if (request->kind == PRK_OUTPUT_OBJECT || request->kind == PRK_OUTPUT_SHARED_LIBRARY) {
        prk_append_argument(arguments, request->kind == PRK_OUTPUT_OBJECT ? "/c" : "/LD", "");
    }
    append_each(arguments, "/I", request->include_directories, request->include_directory_count);
    append_each(arguments, "/D", request->defines, request->define_count);
    append_each(arguments, "", request->sources, request->source_count);
    prk_append_argument(arguments, request->kind == PRK_OUTPUT_OBJECT ? "/Fo" : "/Fe", output_path);
    append_each(arguments, "", request->extra_flags, request->extra_flag_count);
    if (request->kind != PRK_OUTPUT_OBJECT && request->library_count + request->library_directory_count > 0) {
        prk_append_argument(arguments, "/link", "");
        append_each(arguments, "/LIBPATH:", request->library_directories, request->library_directory_count);
        append_libraries(arguments, request, 1);
    }
}

/** @brief Appends the gcc / clang arguments. */
static void append_gcc_arguments(PrkArgumentList* arguments, const PrkCompileRequest* request,
                                 const char* output_path) {
    prk_append_argument(arguments, "-fdiagnostics-color=never", "");
    prk_append_argument(arguments, "-Wall", "");
    prk_append_argument(arguments, "-Wextra", "");
    if (request->is_warnings_as_errors) {
        prk_append_argument(arguments, "-Werror", "");
    }
    if (request->standard != NULL && request->standard[0] != '\0') {
        prk_append_argument(arguments, "-std=", request->standard);
    }
    if (request->kind == PRK_OUTPUT_OBJECT) {
        prk_append_argument(arguments, "-c", "");
    } else if (request->kind == PRK_OUTPUT_SHARED_LIBRARY) {
        prk_append_argument(arguments, "-shared", "");
#if !defined(_WIN32)
        prk_append_argument(arguments, "-fPIC", "");
#endif
    }
    append_each(arguments, "-I", request->include_directories, request->include_directory_count);
    append_each(arguments, "-D", request->defines, request->define_count);
    append_each(arguments, "", request->sources, request->source_count);
    prk_append_argument(arguments, "-o", "");
    prk_append_argument(arguments, "", output_path);
    if (request->kind != PRK_OUTPUT_OBJECT) {
        append_each(arguments, "-L", request->library_directories, request->library_directory_count);
        append_libraries(arguments, request, 0);
    }
    append_each(arguments, "", request->extra_flags, request->extra_flag_count);
}

/** @brief Appends the archiver arguments: ar rcs OUT objects... or lib /nologo /OUT:OUT objects... */
static void append_archiver_arguments(PrkArgumentList* arguments, const PrkCompileRequest* request,
                                      PrkCompilerFamily family, const char* output_path) {
    if (family == PRK_COMPILER_MSVC) {
        prk_append_argument(arguments, "/nologo", "");
        prk_append_argument(arguments, "/OUT:", output_path);
    } else {
        prk_append_argument(arguments, "rcs", "");
        prk_append_argument(arguments, "", output_path);
    }
    append_each(arguments, "", request->sources, request->source_count);
    append_each(arguments, "", request->extra_flags, request->extra_flag_count);
}

PrkStatus prk_build_compiler_arguments(const PrkCompileRequest* request, PrkCompilerFamily family,
                                       const char* output_path, PrkArgumentList* out_arguments) {
    memset(out_arguments, 0, sizeof *out_arguments);
    if (request->kind == PRK_OUTPUT_STATIC_LIBRARY) {
        append_archiver_arguments(out_arguments, request, family, output_path);
    } else if (family == PRK_COMPILER_MSVC) {
        append_msvc_arguments(out_arguments, request, output_path);
    } else {
        append_gcc_arguments(out_arguments, request, output_path);
    }
    if (out_arguments->is_out_of_memory) {
        prk_free_argument_list(out_arguments);
        return PRK_STATUS_OUT_OF_MEMORY;
    }
    return PRK_STATUS_OK;
}

void prk_format_compile_output_path(const char* requested, PrkOutputKind kind, char* out_path, size_t capacity) {
    const size_t requested_length = strlen(requested);
    const size_t copied = requested_length < capacity - 1 ? requested_length : capacity - 1;
    memcpy(out_path, requested, copied);
    out_path[copied] = '\0';
#if defined(_WIN32)
    const char* name = requested;
    for (const char* cursor = requested; *cursor != '\0'; ++cursor) {
        if (*cursor == '/' || *cursor == '\\') {
            name = cursor + 1;
        }
    }
    if (kind == PRK_OUTPUT_EXECUTABLE && strchr(name, '.') == NULL && copied + 5 <= capacity) {
        memcpy(out_path + copied, ".exe", 5);
    }
#else
    (void)kind;
#endif
}
