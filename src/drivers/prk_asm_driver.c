/**
 * @file prk_asm_driver.c
 * @brief Assembler pass (#16): command lines for MASM (ml64 / ml), GAS, and NASM, MSVC discovery for MASM, and the
 *        run through the shared driver tool runner (prk_run_driver_tool).
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "prk_c_driver_internal.h"
#include "proofrunkit/prk_asm_driver.h"

const char* prk_get_assembler_family_name(PrkAssemblerFamily family) {
    switch (family) {
        case PRK_ASSEMBLER_AUTO:
            return "auto";
        case PRK_ASSEMBLER_MASM:
            return "masm";
        case PRK_ASSEMBLER_GAS:
            return "gas";
        case PRK_ASSEMBLER_NASM:
            return "nasm";
    }
    return "unknown";
}

const char* prk_get_default_nasm_format(void) {
#if defined(_WIN32) && (defined(_WIN64) || defined(__x86_64__))
    return "win64";
#elif defined(_WIN32)
    return "win32";
#elif defined(__APPLE__)
    return "macho64";
#elif defined(__i386__)
    return "elf32";
#else
    return "elf64";
#endif
}

PrkAssemblerFamily prk_choose_assembler_family(PrkAssemblerFamily family, const char* source) {
    if (family != PRK_ASSEMBLER_AUTO || source == NULL) {
        return family;
    }
    const char* const dot = strrchr(source, '.');
    const char* const extension = dot != NULL && strpbrk(dot, "/\\") == NULL ? dot : "";
    if (strcmp(extension, ".s") == 0 || strcmp(extension, ".S") == 0) {
        return PRK_ASSEMBLER_GAS;
    }
    if (strcmp(extension, ".nasm") == 0) {
        return PRK_ASSEMBLER_NASM;
    }
    if (strcmp(extension, ".asm") == 0) {
#if defined(_WIN32)
        return PRK_ASSEMBLER_MASM;
#else
        return PRK_ASSEMBLER_NASM;
#endif
    }
    return PRK_ASSEMBLER_AUTO;
}

/** @brief Reports whether the MSVC architecture targets x86 (ml.exe instead of ml64.exe). */
static int is_x86_target(const char* architecture) {
    const char* const chosen = architecture != NULL ? architecture : prk_get_native_msvc_architecture();
    const size_t length = strlen(chosen);
    return strcmp(chosen, "x86") == 0 || (length > 4 && strcmp(chosen + length - 4, "_x86") == 0);
}

/** @brief Appends GAS "--defsym NAME=VALUE" pairs ("NAME" becomes NAME=1). */
static void append_gas_defines(PrkArgumentList* arguments, const PrkAssembleRequest* request) {
    for (size_t index = 0; index < request->define_count; ++index) {
        prk_append_argument(arguments, "--defsym", "");
        prk_append_argument(arguments, request->defines[index], strchr(request->defines[index], '=') ? "" : "=1");
    }
}

PrkStatus prk_build_assembler_arguments(const PrkAssembleRequest* request, PrkAssemblerFamily family,
                                        PrkArgumentList* out_arguments) {
    memset(out_arguments, 0, sizeof *out_arguments);
    const char* const* includes = request->include_directories;
    if (family == PRK_ASSEMBLER_MASM) {
        static const char* const head[] = {"/nologo", "/c", "/W3"};
        for (size_t index = 0; index < sizeof head / sizeof head[0]; ++index) {
            prk_append_argument(out_arguments, head[index], "");
        }
        if (request->is_warnings_as_errors) {
            prk_append_argument(out_arguments, "/WX", "");
        }
        for (size_t index = 0; index < request->include_directory_count; ++index) {
            prk_append_argument(out_arguments, "/I", includes[index]);
        }
        for (size_t index = 0; index < request->define_count; ++index) {
            prk_append_argument(out_arguments, "/D", request->defines[index]);
        }
        prk_append_argument(out_arguments, "/Fo", request->output_path);
    } else if (family == PRK_ASSEMBLER_GAS) {
        if (request->is_warnings_as_errors) {
            prk_append_argument(out_arguments, "--fatal-warnings", "");
        }
        for (size_t index = 0; index < request->include_directory_count; ++index) {
            prk_append_argument(out_arguments, "-I", includes[index]);
        }
        append_gas_defines(out_arguments, request);
        prk_append_argument(out_arguments, "-o", "");
        prk_append_argument(out_arguments, request->output_path, "");
    } else {
        const char* const format =
            request->object_format != NULL ? request->object_format : prk_get_default_nasm_format();
        prk_append_argument(out_arguments, "-f", "");
        prk_append_argument(out_arguments, format, "");
        if (request->is_warnings_as_errors) {
            prk_append_argument(out_arguments, "-Werror", "");
        }
        for (size_t index = 0; index < request->include_directory_count; ++index) {
            prk_append_argument(out_arguments, "-I", includes[index]);
        }
        for (size_t index = 0; index < request->define_count; ++index) {
            prk_append_argument(out_arguments, "-D", request->defines[index]);
        }
        prk_append_argument(out_arguments, "-o", "");
        prk_append_argument(out_arguments, request->output_path, "");
    }
    prk_append_argument(out_arguments, request->source, "");
    for (size_t index = 0; index < request->extra_flag_count; ++index) {
        prk_append_argument(out_arguments, request->extra_flags[index], "");
    }
    if (out_arguments->is_out_of_memory) {
        prk_free_argument_list(out_arguments);
        return PRK_STATUS_OUT_OF_MEMORY;
    }
    return PRK_STATUS_OK;
}

/** @brief Chooses the assembler program: the override, ml64/ml next to a discovered cl.exe, or the default. */
static void choose_assembler_program(const PrkAssembleRequest* request, PrkAssemblerFamily family,
                                     const PrkMsvcToolchain* toolchain, char* out_program, size_t capacity) {
    const char* const masm = is_x86_target(request->msvc_architecture) ? "ml" : "ml64";
    if (request->assembler_program != NULL && request->assembler_program[0] != '\0') {
        (void)snprintf(out_program, capacity, "%s", request->assembler_program);
    } else if (toolchain != NULL) {
        const char* const slash = strrchr(toolchain->cl_path, '\\');
        const int folder_length = slash != NULL ? (int)(slash - toolchain->cl_path) + 1 : 0;
        (void)snprintf(out_program, capacity, "%.*s%s.exe", folder_length, toolchain->cl_path, masm);
    } else {
        (void)snprintf(out_program, capacity, "%s",
                       family == PRK_ASSEMBLER_MASM  ? masm
                       : family == PRK_ASSEMBLER_GAS ? "as"
                                                     : "nasm");
    }
}

/** @brief Runs one assembler and fills @p out_result. */
static PrkStatus run_assembler(const PrkProcessRunner* runner, const PrkAssembleRequest* request,
                               PrkAssemblerFamily family, const PrkMsvcToolchain* toolchain,
                               PrkCompileResult* out_result) {
    memset(out_result, 0, sizeof *out_result);
    out_result->family = family == PRK_ASSEMBLER_MASM ? PRK_COMPILER_MSVC : PRK_COMPILER_GCC;
    out_result->kind = PRK_OUTPUT_OBJECT;
    out_result->is_msvc_discovered = toolchain != NULL;
    (void)snprintf(out_result->output_path, sizeof out_result->output_path, "%s", request->output_path);
    char program[1100];
    choose_assembler_program(request, family, toolchain, program, sizeof program);
    PrkArgumentList arguments;
    PrkStatus status = prk_build_assembler_arguments(request, family, &arguments);
    if (status != PRK_STATUS_OK) {
        return status;
    }
    const PrkDriverTool tool = {program,   &arguments,          request->working_directory,
                                toolchain, request->timeout_ms, out_result->family};
    status = prk_run_driver_tool(runner, &tool, out_result);
    prk_free_argument_list(&arguments);
    return status;
}

/** @brief MASM: a cached toolchain, else ml64 / ml from PATH, else MSVC discovery. */
static PrkStatus assemble_with_masm(const PrkProcessRunner* runner, const PrkAssembleRequest* request,
                                    PrkCompileResult* out_result) {
    PrkMsvcToolchain local_toolchain;
    memset(&local_toolchain, 0, sizeof local_toolchain);
    PrkMsvcToolchain* const toolchain = request->msvc_toolchain != NULL ? request->msvc_toolchain : &local_toolchain;
    const int has_override = request->assembler_program != NULL && request->assembler_program[0] != '\0';
    PrkStatus status = toolchain->is_found && !has_override
                           ? run_assembler(runner, request, PRK_ASSEMBLER_MASM, toolchain, out_result)
                           : run_assembler(runner, request, PRK_ASSEMBLER_MASM, NULL, out_result);
    const int needs_discovery = status == PRK_STATUS_OK && !out_result->is_msvc_discovered && !has_override &&
                                out_result->process.outcome == PRK_PROCESS_FAILED_TO_START;
    if (needs_discovery && !toolchain->is_attempted) {
        status = prk_discover_msvc_toolchain(runner, request->msvc_architecture, toolchain);
    }
    if (needs_discovery && status == PRK_STATUS_OK && toolchain->is_found) {
        prk_release_compile_result(out_result);
        status = run_assembler(runner, request, PRK_ASSEMBLER_MASM, toolchain, out_result);
    } else if (needs_discovery && status == PRK_STATUS_OK) {
        (void)snprintf(out_result->process.error_message, sizeof out_result->process.error_message,
                       "%.60s is not on PATH and MSVC discovery failed: %.140s", out_result->compiler_program,
                       toolchain->error_message);
    } else if (status != PRK_STATUS_OK) {
        prk_release_compile_result(out_result);
    }
    prk_release_msvc_toolchain(&local_toolchain);
    return status;
}

PrkStatus prk_assemble_source(const PrkProcessRunner* runner, const PrkAssembleRequest* request,
                              PrkCompileResult* out_result) {
    if (runner == NULL || runner->spawn_process == NULL || request == NULL || out_result == NULL ||
        request->source == NULL || request->source[0] == '\0' || request->output_path == NULL ||
        request->output_path[0] == '\0' || (int)request->family < 0 || request->family > PRK_ASSEMBLER_NASM ||
        (request->assembler_program != NULL && request->family == PRK_ASSEMBLER_AUTO) ||
        (request->msvc_architecture != NULL && !prk_is_msvc_architecture_allowed(request->msvc_architecture)) ||
        (request->include_directory_count != 0 && request->include_directories == NULL) ||
        (request->define_count != 0 && request->defines == NULL) ||
        (request->extra_flag_count != 0 && request->extra_flags == NULL)) {
        return PRK_STATUS_INVALID_ARGUMENT;
    }
    const PrkAssemblerFamily family = prk_choose_assembler_family(request->family, request->source);
    if (family == PRK_ASSEMBLER_AUTO) {
        return PRK_STATUS_INVALID_ARGUMENT; /* Unknown extension: the caller must name the assembler. */
    }
    return family == PRK_ASSEMBLER_MASM ? assemble_with_masm(runner, request, out_result)
                                        : run_assembler(runner, request, family, NULL, out_result);
}
