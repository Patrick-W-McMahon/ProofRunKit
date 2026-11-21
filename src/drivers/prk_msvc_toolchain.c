/**
 * @file prk_msvc_toolchain.c
 * @brief MSVC discovery (#14): vswhere (run directly) for the Visual Studio folder, then vcvarsall.bat in cmd.exe
 *        for the environment, from which the cl.exe / lib.exe paths follow. See prk_msvc_toolchain.h.
 *
 * Runs only through the injected PrkProcessRunner; the parsers are pure and unit-tested on every platform.
 * Script input reaches cmd.exe only as an architecture from a fixed list.
 */
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "prk_c_driver_internal.h"
#include "proofrunkit/prk_msvc_toolchain.h"

/** @brief Used when %ProgramFiles(x86)% is not set (a 32-bit Windows, or a test host). */
#define PRK_DEFAULT_PROGRAM_FILES_X86 "C:\\Program Files (x86)"

const char* prk_get_native_msvc_architecture(void) {
#if defined(_M_ARM64) || defined(__aarch64__)
    return "arm64";
#elif defined(_M_IX86) || defined(__i386__)
    return "x86";
#else
    return "x64";
#endif
}

int prk_is_msvc_architecture_allowed(const char* architecture) {
    static const char* const allowed[] = {"x64",       "x86",   "arm64",     "x64_x86",     "x64_arm64", "x86_x64",
                                          "x86_arm64", "amd64", "amd64_x86", "amd64_arm64", NULL};
    for (size_t index = 0; architecture != NULL && allowed[index] != NULL; ++index) {
        if (strcmp(architecture, allowed[index]) == 0) {
            return 1;
        }
    }
    return 0;
}

/** @brief Finds the next line in [*cursor, end); trims CR and surrounding blanks. @return 0 at the end. */
static int read_next_line(const char** cursor, const char* end, const char** out_start, size_t* out_length) {
    if (*cursor >= end) {
        return 0;
    }
    const char* start = *cursor;
    const char* stop = start;
    while (stop < end && *stop != '\n') {
        ++stop;
    }
    *cursor = stop < end ? stop + 1 : end;
    while (start < stop && (*start == ' ' || *start == '\t')) {
        ++start;
    }
    while (stop > start && (stop[-1] == '\r' || stop[-1] == ' ' || stop[-1] == '\t')) {
        --stop;
    }
    *out_start = start;
    *out_length = (size_t)(stop - start);
    return 1;
}

/** @brief Copies a line into a terminated buffer. @return 1 when it fit. */
static int copy_line(const char* start, size_t length, char* out_text, size_t capacity) {
    if (length >= capacity) {
        return 0;
    }
    memcpy(out_text, start, length);
    out_text[length] = '\0';
    return 1;
}

int prk_parse_vswhere_output(const char* text, size_t length, char* out_path, size_t capacity) {
    const char* cursor = text != NULL ? text : "";
    const char* const end = cursor + (text != NULL ? length : 0);
    const char* start = NULL;
    size_t line_length = 0;
    while (read_next_line(&cursor, end, &start, &line_length)) {
        if (line_length > 0) {
            return memchr(start, '"', line_length) == NULL && memchr(start, '%', line_length) == NULL &&
                   copy_line(start, line_length, out_path, capacity);
        }
    }
    return 0;
}

void prk_build_vswhere_path(const char* program_files_x86, char* out_path, size_t capacity) {
    const char* const folder =
        program_files_x86 != NULL && program_files_x86[0] != '\0' ? program_files_x86 : PRK_DEFAULT_PROGRAM_FILES_X86;
    (void)snprintf(out_path, capacity, "%s\\Microsoft Visual Studio\\Installer\\vswhere.exe", folder);
}

PrkStatus prk_build_vcvars_arguments(const char* installation_path, const char* architecture,
                                     PrkArgumentList* out_arguments) {
    memset(out_arguments, 0, sizeof *out_arguments);
    /* /u: cmd's own output (the `set` listing) is UTF-16LE, so any environment text survives unchanged. */
    static const char* const head[] = {"/d", "/u", "/c", "call"};
    for (size_t index = 0; index < sizeof head / sizeof head[0]; ++index) {
        prk_append_argument(out_arguments, "", head[index]);
    }
    char script[1200];
    (void)snprintf(script, sizeof script, "%s\\VC\\Auxiliary\\Build\\vcvarsall.bat", installation_path);
    prk_append_argument(out_arguments, "", script);
    prk_append_argument(out_arguments, "", architecture);
    static const char* const tail[] = {">nul", "&&", "set"};
    for (size_t index = 0; index < sizeof tail / sizeof tail[0]; ++index) {
        prk_append_argument(out_arguments, "", tail[index]);
    }
    if (out_arguments->is_out_of_memory) {
        prk_free_argument_list(out_arguments);
        return PRK_STATUS_OUT_OF_MEMORY;
    }
    return PRK_STATUS_OK;
}

/** @brief Appends one "NAME=VALUE" entry to the toolchain environment. @return 0, or -1 when out of memory. */
static int add_environment_entry(PrkMsvcToolchain* toolchain, const char* start, size_t length) {
    char** const grown =
        (char**)realloc(toolchain->environment, (toolchain->environment_count + 1) * sizeof *toolchain->environment);
    if (grown == NULL) {
        return -1;
    }
    toolchain->environment = grown;
    char* const entry = (char*)malloc(length + 1);
    if (entry == NULL) {
        return -1;
    }
    memcpy(entry, start, length);
    entry[length] = '\0';
    toolchain->environment[toolchain->environment_count++] = entry;
    return 0;
}

/** @brief Returns the value of @p name (case-insensitive, as on Windows) in the toolchain environment, or NULL. */
static const char* find_environment_value(const PrkMsvcToolchain* toolchain, const char* name) {
    const size_t name_length = strlen(name);
    for (size_t index = 0; index < toolchain->environment_count; ++index) {
        const char* const entry = toolchain->environment[index];
        size_t position = 0;
        while (position < name_length && entry[position] != '\0' &&
               tolower((unsigned char)entry[position]) == tolower((unsigned char)name[position])) {
            ++position;
        }
        if (position == name_length && entry[position] == '=') {
            return entry + position + 1;
        }
    }
    return NULL;
}

/** @brief Sets cl_path and lib_path from VCToolsInstallDir and the VSCMD host/target architectures. @return 1 if set.
 */
static int set_tool_paths(PrkMsvcToolchain* toolchain) {
    const char* const tools = find_environment_value(toolchain, "VCToolsInstallDir");
    const char* const host = find_environment_value(toolchain, "VSCMD_ARG_HOST_ARCH");
    const char* const target = find_environment_value(toolchain, "VSCMD_ARG_TGT_ARCH");
    if (tools == NULL || host == NULL || target == NULL || tools[0] == '\0') {
        return 0;
    }
    const size_t length = strlen(tools);
    const char* const separator = tools[length - 1] == '\\' ? "" : "\\";
    const int written = snprintf(toolchain->cl_path, sizeof toolchain->cl_path, "%s%sbin\\Host%s\\%s\\cl.exe", tools,
                                 separator, host, target);
    if (written < 0 || (size_t)written >= sizeof toolchain->cl_path) {
        toolchain->cl_path[0] = '\0';
        return 0;
    }
    (void)snprintf(toolchain->lib_path, sizeof toolchain->lib_path, "%.*slib.exe", written - 6, toolchain->cl_path);
    return 1;
}

PrkStatus prk_parse_vcvars_output(const char* text, size_t length, PrkMsvcToolchain* out_toolchain) {
    char* decoded = NULL;
    size_t decoded_length = 0;
    if (prk_decode_utf16le_if_wide(text, length, &decoded, &decoded_length) != PRK_STATUS_OK) {
        return PRK_STATUS_OUT_OF_MEMORY;
    }
    const char* cursor = decoded != NULL ? decoded : (text != NULL ? text : "");
    const char* const end = cursor + (decoded != NULL ? decoded_length : (text != NULL ? length : 0));
    const char* start = NULL;
    size_t line_length = 0;
    PrkStatus status = PRK_STATUS_OK;
    while (status == PRK_STATUS_OK && read_next_line(&cursor, end, &start, &line_length)) {
        if (line_length > 1 && start[0] != '=' && memchr(start, '=', line_length) != NULL &&
            add_environment_entry(out_toolchain, start, line_length) != 0) {
            status = PRK_STATUS_OUT_OF_MEMORY;
        }
    }
    free(decoded);
    if (status != PRK_STATUS_OK) {
        return status;
    }
    out_toolchain->is_found = set_tool_paths(out_toolchain);
    if (!out_toolchain->is_found) {
        (void)snprintf(out_toolchain->error_message, sizeof out_toolchain->error_message,
                       "vcvarsall.bat %s ran but did not set VCToolsInstallDir and VSCMD_ARG_HOST_ARCH/TGT_ARCH",
                       out_toolchain->architecture);
    }
    return PRK_STATUS_OK;
}

/** @brief Runs @p program with @p arguments and the parent environment. */
static PrkStatus run_discovery_step(const PrkProcessRunner* runner, const char* program, const char* const* arguments,
                                    size_t count, PrkProcessResult* out_result) {
    PrkProcessRequest request;
    memset(&request, 0, sizeof request);
    request.executable = program;
    request.arguments = arguments;
    request.argument_count = count;
    request.is_environment_inherited = 1;
    request.timeout_ms = PRK_MSVC_DISCOVERY_TIMEOUT_MS;
    return runner->spawn_process(runner->runner_state, &request, out_result);
}

/** @brief Describes a failed discovery step in the toolchain. */
static void describe_failed_step(const char* step, const PrkProcessResult* result, PrkMsvcToolchain* toolchain) {
    if (result->outcome == PRK_PROCESS_EXITED) {
        (void)snprintf(toolchain->error_message, sizeof toolchain->error_message, "%s failed with exit code %d", step,
                       result->exit_code);
    } else {
        (void)snprintf(toolchain->error_message, sizeof toolchain->error_message, "%s did not run: %.180s", step,
                       result->outcome == PRK_PROCESS_FAILED_TO_START ? result->error_message
                                                                      : prk_get_process_outcome_name(result->outcome));
    }
}

/** @brief Step 1: vswhere, run directly from %ProgramFiles(x86)%. @return PRK_STATUS_OK (found or not) or an error. */
static PrkStatus find_installation(const PrkProcessRunner* runner, PrkMsvcToolchain* toolchain) {
    static const char* const arguments[] = {
        "-latest",   "-products",        "*",     "-requires", "Microsoft.VisualStudio.Component.VC.Tools.x86.x64",
        "-property", "installationPath", "-utf8", "-nologo"};
    char program_files[1024];
    char program[1100];
    const int is_set = prk_read_environment_variable("ProgramFiles(x86)", program_files, sizeof program_files);
    prk_build_vswhere_path(is_set ? program_files : NULL, program, sizeof program);
    PrkProcessResult result;
    memset(&result, 0, sizeof result);
    const PrkStatus status =
        run_discovery_step(runner, program, arguments, sizeof arguments / sizeof arguments[0], &result);
    if (status != PRK_STATUS_OK) {
        return status;
    }
    if (result.outcome != PRK_PROCESS_EXITED || result.exit_code != 0) {
        describe_failed_step("vswhere (Visual Studio Installer)", &result, toolchain);
    } else if (!prk_parse_vswhere_output(result.standard_output, result.standard_output_length,
                                         toolchain->installation_path, sizeof toolchain->installation_path)) {
        (void)snprintf(toolchain->error_message, sizeof toolchain->error_message,
                       "vswhere found no Visual Studio with the C++ build tools (VC.Tools.x86.x64)");
    }
    prk_release_process_result(&result);
    return PRK_STATUS_OK;
}

/** @brief Step 2: vcvarsall.bat. @return PRK_STATUS_OK (found or not) or an error status. */
static PrkStatus load_vcvars(const PrkProcessRunner* runner, PrkMsvcToolchain* toolchain) {
    PrkArgumentList arguments;
    PrkStatus status = prk_build_vcvars_arguments(toolchain->installation_path, toolchain->architecture, &arguments);
    if (status != PRK_STATUS_OK) {
        return status;
    }
    PrkProcessResult result;
    memset(&result, 0, sizeof result);
    status = run_discovery_step(runner, "cmd.exe", (const char* const*)arguments.items, arguments.count, &result);
    prk_free_argument_list(&arguments);
    if (status != PRK_STATUS_OK) {
        return status;
    }
    if (result.outcome != PRK_PROCESS_EXITED || result.exit_code != 0) {
        describe_failed_step("vcvarsall.bat", &result, toolchain);
    } else {
        status = prk_parse_vcvars_output(result.standard_output, result.standard_output_length, toolchain);
    }
    prk_release_process_result(&result);
    return status;
}

PrkStatus prk_discover_msvc_toolchain(const PrkProcessRunner* runner, const char* architecture,
                                      PrkMsvcToolchain* out_toolchain) {
    const char* const chosen = architecture != NULL ? architecture : prk_get_native_msvc_architecture();
    if (runner == NULL || runner->spawn_process == NULL || out_toolchain == NULL ||
        !prk_is_msvc_architecture_allowed(chosen)) {
        return PRK_STATUS_INVALID_ARGUMENT;
    }
    prk_release_msvc_toolchain(out_toolchain);
    out_toolchain->is_attempted = 1;
    (void)snprintf(out_toolchain->architecture, sizeof out_toolchain->architecture, "%s", chosen);
    PrkStatus status = find_installation(runner, out_toolchain);
    if (status == PRK_STATUS_OK && out_toolchain->installation_path[0] != '\0') {
        status = load_vcvars(runner, out_toolchain);
    }
    if (status != PRK_STATUS_OK) {
        prk_release_msvc_toolchain(out_toolchain);
    }
    return status;
}

void prk_release_msvc_toolchain(PrkMsvcToolchain* toolchain) {
    if (toolchain == NULL) {
        return;
    }
    for (size_t index = 0; index < toolchain->environment_count; ++index) {
        free(toolchain->environment[index]);
    }
    free(toolchain->environment);
    memset(toolchain, 0, sizeof *toolchain);
}
