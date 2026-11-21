/**
 * @file prk_js_assemble.c
 * @brief prk.assemble for JavaScript test scripts (#16); the camelCase twin of prk.assemble in prk_lua_compile.c.
 *
 *   const object = prk.assemble({source: 'add.asm', output: 'add.obj', assembler: 'auto', program: undefined,
 *                                includes: ['inc'], defines: ['FAST'], flags: [], werror: true, format: 'win64',
 *                                msvcArch: 'x64', cwd: undefined, timeoutMs: 60000});
 *
 * The result has the prk.compile fields with `assembler` ("masm", "gas", "nasm") instead of `compiler`; link the
 * object with prk.compile. A missing assembler is a result, not an exception.
 */
#include <string.h>

#include "prk_js_internal.h"
#include "proofrunkit/prk_asm_driver.h"

/** @brief Option names of the borrowed strings, in PrkJsAssembleStrings order. */
static const char* const prk_js_assemble_text_keys[7] = {"source", "output",   "assembler", "program",
                                                         "format", "msvcArch", "cwd"};
static const char* const prk_js_assemble_list_keys[3] = {"includes", "defines", "flags"};

/** @brief Every C string borrowed for one request; released together. */
typedef struct PrkJsAssembleStrings {
    const char* texts[7];     /**< source, output, assembler, program, format, msvcArch, cwd. */
    PrkJsStringList lists[3]; /**< includes, defines, flags. */
} PrkJsAssembleStrings;

static void prk_release_js_assemble_strings(JSContext* context, PrkJsAssembleStrings* strings) {
    for (size_t index = 0; index < 7; ++index) {
        JS_FreeCString(context, strings->texts[index]);
    }
    for (size_t index = 0; index < 3; ++index) {
        prk_release_js_string_list(context, &strings->lists[index]);
    }
    memset(strings, 0, sizeof *strings);
}

/** @brief Checks option combinations. @return 0, or -1 with an exception pending. */
static int prk_check_js_assemble_request(JSContext* context, const PrkAssembleRequest* request) {
    const char* problem = NULL;
    if (request->source == NULL || request->output_path == NULL) {
        problem = "prk.assemble: source and output are required";
    } else if (request->assembler_program != NULL && request->family == PRK_ASSEMBLER_AUTO) {
        problem = "prk.assemble: program needs assembler 'masm', 'gas', or 'nasm'";
    } else if (prk_choose_assembler_family(request->family, request->source) == PRK_ASSEMBLER_AUTO) {
        problem = "prk.assemble: unknown source extension; name the assembler";
    } else if (request->msvc_architecture != NULL && !prk_is_msvc_architecture_allowed(request->msvc_architecture)) {
        problem = "prk.assemble: msvcArch must be a vcvarsall architecture such as 'x64' or 'x86'";
    }
    if (problem != NULL) {
        JS_ThrowTypeError(context, "%s", problem);
        return -1;
    }
    return 0;
}

/** @brief Reads every option. @return 0, or -1 with an exception pending. */
static int prk_read_js_assemble_request(JSContext* context, const PrkJsScript* script, JSValueConst options,
                                        PrkJsAssembleStrings* strings, PrkAssembleRequest* out_request) {
    static const char* const known[] = {"source", "output", "assembler", "program", "includes",  "defines", "flags",
                                        "werror", "format", "msvcArch",  "cwd",     "timeoutMs", NULL};
    static const char* const families[] = {"auto", "masm", "gas", "nasm", NULL};
    int status = prk_check_js_option_keys(context, options, known, "prk.assemble");
    for (size_t index = 0; status == 0 && index < 7; ++index) {
        status = prk_read_js_text_option(context, options, prk_js_assemble_text_keys[index], "prk.assemble",
                                         &strings->texts[index]);
    }
    for (size_t index = 0; status == 0 && index < 3; ++index) {
        status = prk_read_js_list_option(context, options, prk_js_assemble_list_keys[index], "prk.assemble",
                                         &strings->lists[index]);
    }
    const int family =
        status == 0 ? prk_find_js_choice(context, "assembler", strings->texts[2], families, "prk.assemble") : -1;
    if (family < 0 ||
        prk_read_js_werror_and_timeout(context, script, options, "prk.assemble", &out_request->is_warnings_as_errors,
                                       &out_request->timeout_ms) != 0) {
        return -1;
    }
    out_request->family = (PrkAssemblerFamily)family;
    out_request->source = strings->texts[0];
    out_request->output_path = strings->texts[1];
    out_request->assembler_program = strings->texts[3];
    out_request->object_format = strings->texts[4];
    out_request->msvc_architecture = strings->texts[5];
    out_request->working_directory = strings->texts[6];
    out_request->include_directories = strings->lists[0].items;
    out_request->include_directory_count = strings->lists[0].count;
    out_request->defines = strings->lists[1].items;
    out_request->define_count = strings->lists[1].count;
    out_request->extra_flags = strings->lists[2].items;
    out_request->extra_flag_count = strings->lists[2].count;
    out_request->msvc_toolchain = script->host.msvc_toolchain;
    return prk_check_js_assemble_request(context, out_request);
}

/** @brief prk.assemble(options) -> result object. */
static JSValue prk_js_assemble(JSContext* context, JSValueConst this_value, int argc, JSValueConst* argv) {
    (void)this_value;
    const PrkJsScript* const script = prk_find_js_script(context);
    if (argc < 1 || !JS_IsObject(argv[0])) {
        return JS_ThrowTypeError(context, "prk.assemble(options) needs an options object");
    }
    if (script->host.process_runner.spawn_process == NULL) {
        return JS_ThrowTypeError(context, "prk.assemble: process spawn is not available in this host");
    }
    PrkJsAssembleStrings strings;
    memset(&strings, 0, sizeof strings);
    PrkAssembleRequest request;
    memset(&request, 0, sizeof request);
    if (prk_read_js_assemble_request(context, script, argv[0], &strings, &request) != 0) {
        prk_release_js_assemble_strings(context, &strings);
        return JS_EXCEPTION;
    }
    const PrkAssemblerFamily family = prk_choose_assembler_family(request.family, request.source);
    PrkCompileResult result;
    memset(&result, 0, sizeof result);
    const PrkStatus status = prk_assemble_source(&script->host.process_runner, &request, &result);
    prk_release_js_assemble_strings(context, &strings);
    if (status != PRK_STATUS_OK) {
        return JS_ThrowTypeError(context, "prk.assemble: %s", prk_get_status_name(status));
    }
    JSValue object = prk_make_js_compile_result(context, &result);
    prk_release_compile_result(&result);
    if (!JS_IsException(object)) {
        JSAtom compiler = JS_NewAtom(context, "compiler");
        (void)JS_DeleteProperty(context, object, compiler, 0);
        JS_FreeAtom(context, compiler);
        JS_SetPropertyStr(context, object, "assembler", JS_NewString(context, prk_get_assembler_family_name(family)));
    }
    return object;
}

int prk_add_js_assemble_api(JSContext* context, JSValueConst prk) {
    JSValue function = JS_NewCFunction(context, &prk_js_assemble, "assemble", 1);
    if (JS_IsException(function)) {
        return -1;
    }
    return JS_SetPropertyStr(context, prk, "assemble", function) < 0 ? -1 : 0;
}
