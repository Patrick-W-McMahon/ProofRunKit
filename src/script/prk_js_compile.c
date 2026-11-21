/**
 * @file prk_js_compile.c
 * @brief prk.compile for JavaScript test scripts (#14); the camelCase twin of prk_lua_compile.c.
 *
 *   const result = prk.compile({sources: ['hello.c'], output: 'build/hello', language: 'c', compiler: 'auto',
 *                               kind: 'executable', program: undefined, archiver: undefined, msvcArch: 'x64',
 *                               std: 'c17', includes: ['include'], defines: ['NDEBUG'], flags: ['-O2'],
 *                               libraries: ['m'], libraryDirs: ['lib'], werror: true, timeoutMs: 120000});
 *
 * The result has every prk.runProcess field (ok, outcome, exitCode, stdout, stderr, elapsedMs, error) plus
 * compiler, program, command, output, kind, msvcDiscovered, errors, warnings, and diagnostics (an array of
 * {severity, file, line, column, code, message}). A missing compiler is a result, not an exception.
 */
#include <stdlib.h>
#include <string.h>

#include "prk_js_internal.h"
#include "proofrunkit/prk_c_driver.h"

/** @brief Every C string borrowed for one compile request; released together. */
typedef struct PrkJsCompileStrings {
    const char* texts[9];     /**< output, language, compiler, program, std, cwd, kind, archiver, msvcArch. */
    PrkJsStringList lists[6]; /**< sources, includes, defines, flags, libraries, libraryDirs. */
} PrkJsCompileStrings;

static const char* const prk_js_compile_text_keys[9] = {"output", "language", "compiler", "program", "std",
                                                        "cwd",    "kind",     "archiver", "msvcArch"};
static const char* const prk_js_compile_list_keys[6] = {"sources", "includes",  "defines",
                                                        "flags",   "libraries", "libraryDirs"};

static void prk_release_js_compile_strings(JSContext* context, PrkJsCompileStrings* strings) {
    for (size_t index = 0; index < 9; ++index) {
        JS_FreeCString(context, strings->texts[index]);
    }
    for (size_t list = 0; list < 6; ++list) {
        prk_release_js_string_list(context, &strings->lists[list]);
    }
    memset(strings, 0, sizeof *strings);
}

/** @brief Reads werror and timeoutMs. @return 0, or -1 with an exception pending. */
static int prk_read_js_compile_flags(JSContext* context, const PrkJsScript* script, JSValueConst options,
                                     PrkCompileRequest* out_request) {
    return prk_read_js_werror_and_timeout(context, script, options, "prk.compile", &out_request->is_warnings_as_errors,
                                          &out_request->timeout_ms);
}

/** @brief Points @p out_request at the borrowed strings. */
static void prk_fill_js_compile_request(const PrkJsCompileStrings* strings, PrkCompileRequest* out_request) {
    out_request->output_path = strings->texts[0];
    out_request->compiler_program = strings->texts[3];
    out_request->standard = strings->texts[4];
    out_request->working_directory = strings->texts[5];
    out_request->archiver_program = strings->texts[7];
    out_request->msvc_architecture = strings->texts[8];
    out_request->sources = strings->lists[0].items;
    out_request->source_count = strings->lists[0].count;
    out_request->include_directories = strings->lists[1].items;
    out_request->include_directory_count = strings->lists[1].count;
    out_request->defines = strings->lists[2].items;
    out_request->define_count = strings->lists[2].count;
    out_request->extra_flags = strings->lists[3].items;
    out_request->extra_flag_count = strings->lists[3].count;
    out_request->libraries = strings->lists[4].items;
    out_request->library_count = strings->lists[4].count;
    out_request->library_directories = strings->lists[5].items;
    out_request->library_directory_count = strings->lists[5].count;
}

/** @brief Checks option combinations. @return 0, or -1 with an exception pending. */
static int prk_check_js_compile_request(JSContext* context, const PrkCompileRequest* request) {
    const char* problem = NULL;
    if (request->output_path == NULL || request->output_path[0] == '\0' || request->source_count == 0) {
        problem = "prk.compile: needs output and at least one source";
    } else if (request->kind == PRK_OUTPUT_OBJECT && request->source_count != 1) {
        problem = "prk.compile: kind 'object' compiles exactly one source";
    } else if ((request->compiler_program != NULL || request->archiver_program != NULL) &&
               request->family == PRK_COMPILER_AUTO) {
        problem = "prk.compile: program and archiver need compiler 'gcc', 'clang', or 'msvc'";
    } else if (request->msvc_architecture != NULL && !prk_is_msvc_architecture_allowed(request->msvc_architecture)) {
        problem = "prk.compile: msvcArch must be a vcvarsall architecture such as 'x64' or 'x86'";
    }
    if (problem != NULL) {
        JS_ThrowTypeError(context, "%s", problem);
        return -1;
    }
    return 0;
}

/** @brief Reads every option. @return 0, or -1 with an exception pending. */
static int prk_read_js_compile_request(JSContext* context, const PrkJsScript* script, JSValueConst options,
                                       PrkJsCompileStrings* strings, PrkCompileRequest* out_request) {
    static const char* const families[] = {"auto", "gcc", "clang", "msvc", NULL};
    static const char* const languages[] = {"c", "c++", NULL};
    static const char* const kinds[] = {"executable", "object", "static_library", "shared_library", NULL};
    static const char* const known[] = {"sources",   "output",      "language", "compiler", "program",   "std",
                                        "includes",  "defines",     "flags",    "werror",   "cwd",       "kind",
                                        "libraries", "libraryDirs", "archiver", "msvcArch", "timeoutMs", NULL};
    int status = prk_check_js_option_keys(context, options, known, "prk.compile");
    for (size_t index = 0; status == 0 && index < 9; ++index) {
        status = prk_read_js_text_option(context, options, prk_js_compile_text_keys[index], "prk.compile",
                                         &strings->texts[index]);
    }
    for (size_t index = 0; status == 0 && index < 6; ++index) {
        status = prk_read_js_list_option(context, options, prk_js_compile_list_keys[index], "prk.compile",
                                         &strings->lists[index]);
    }
    const int language =
        status == 0 ? prk_find_js_choice(context, "language", strings->texts[1], languages, "prk.compile") : -1;
    const int family =
        language >= 0 ? prk_find_js_choice(context, "compiler", strings->texts[2], families, "prk.compile") : -1;
    const int kind = family >= 0 ? prk_find_js_choice(context, "kind", strings->texts[6], kinds, "prk.compile") : -1;
    if (kind < 0 || prk_read_js_compile_flags(context, script, options, out_request) != 0) {
        return -1;
    }
    out_request->family = (PrkCompilerFamily)family;
    out_request->language = (PrkSourceLanguage)language;
    out_request->kind = (PrkOutputKind)kind;
    out_request->msvc_toolchain = script->host.msvc_toolchain;
    prk_fill_js_compile_request(strings, out_request);
    return prk_check_js_compile_request(context, out_request);
}

/** @brief Builds one diagnostic object. */
static JSValue prk_make_js_diagnostic(JSContext* context, const PrkCompilerDiagnostic* diagnostic) {
    static const char* const severities[] = {"error", "warning", "note"};
    JSValue object = JS_NewObject(context);
    if (!JS_IsException(object)) {
        JS_SetPropertyStr(context, object, "severity", JS_NewString(context, severities[diagnostic->severity]));
        JS_SetPropertyStr(context, object, "file", JS_NewString(context, diagnostic->file));
        JS_SetPropertyStr(context, object, "line", JS_NewInt64(context, (int64_t)diagnostic->line));
        JS_SetPropertyStr(context, object, "column", JS_NewInt64(context, (int64_t)diagnostic->column));
        JS_SetPropertyStr(context, object, "code", JS_NewString(context, diagnostic->code));
        JS_SetPropertyStr(context, object, "message", JS_NewString(context, diagnostic->message));
    }
    return object;
}

JSValue prk_make_js_compile_result(JSContext* context, const PrkCompileResult* result) {
    JSValue object = prk_make_js_process_result(context, &result->process);
    JSValue diagnostics = JS_IsException(object) ? JS_EXCEPTION : JS_NewArray(context);
    if (JS_IsException(diagnostics)) {
        JS_FreeValue(context, object);
        return JS_EXCEPTION;
    }
    for (size_t index = 0; index < result->diagnostic_count; ++index) {
        JS_SetPropertyInt64(context, diagnostics, (int64_t)index,
                            prk_make_js_diagnostic(context, &result->diagnostics[index]));
    }
    JS_SetPropertyStr(context, object, "diagnostics", diagnostics);
    JS_SetPropertyStr(context, object, "compiler", JS_NewString(context, prk_get_compiler_family_name(result->family)));
    JS_SetPropertyStr(context, object, "program", JS_NewString(context, result->compiler_program));
    JS_SetPropertyStr(context, object, "command",
                      JS_NewString(context, result->command_line != NULL ? result->command_line : ""));
    JS_SetPropertyStr(context, object, "output", JS_NewString(context, result->output_path));
    JS_SetPropertyStr(context, object, "kind", JS_NewString(context, prk_get_output_kind_name(result->kind)));
    JS_SetPropertyStr(context, object, "msvcDiscovered", JS_NewBool(context, result->is_msvc_discovered));
    JS_SetPropertyStr(context, object, "errors", JS_NewInt64(context, (int64_t)result->error_count));
    JS_SetPropertyStr(context, object, "warnings", JS_NewInt64(context, (int64_t)result->warning_count));
    return object;
}

/** @brief prk.compile(options) -> result object. */
static JSValue prk_js_compile(JSContext* context, JSValueConst this_value, int argc, JSValueConst* argv) {
    (void)this_value;
    const PrkJsScript* const script = prk_find_js_script(context);
    if (argc < 1 || !JS_IsObject(argv[0])) {
        return JS_ThrowTypeError(context, "prk.compile(options) needs an options object");
    }
    if (script->host.process_runner.spawn_process == NULL) {
        return JS_ThrowTypeError(context, "prk.compile: process spawn is not available in this host");
    }
    PrkJsCompileStrings strings;
    memset(&strings, 0, sizeof strings);
    PrkCompileRequest request;
    memset(&request, 0, sizeof request);
    if (prk_read_js_compile_request(context, script, argv[0], &strings, &request) != 0) {
        prk_release_js_compile_strings(context, &strings);
        return JS_EXCEPTION;
    }
    PrkCompileResult result;
    memset(&result, 0, sizeof result);
    const PrkStatus status = prk_compile_program(&script->host.process_runner, &request, &result);
    prk_release_js_compile_strings(context, &strings);
    if (status != PRK_STATUS_OK) {
        return JS_ThrowTypeError(context, "prk.compile: %s", prk_get_status_name(status));
    }
    JSValue object = prk_make_js_compile_result(context, &result);
    prk_release_compile_result(&result);
    return object;
}

int prk_add_js_compile_api(JSContext* context, JSValueConst prk) {
    JSValue function = JS_NewCFunction(context, &prk_js_compile, "compile", 1);
    if (JS_IsException(function)) {
        return -1;
    }
    return JS_SetPropertyStr(context, prk, "compile", function) < 0 ? -1 : 0;
}
