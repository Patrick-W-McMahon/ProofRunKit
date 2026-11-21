/**
 * @file prk_js_process.c
 * @brief prk.runProcess for JavaScript test scripts (#12 over the #13 spawn layer); the camelCase twin of
 *        prk.run_process in prk_lua_process.c.
 *
 *   const result = prk.runProcess({program: 'gcc', args: ['-c', 'a.c'], cwd: 'build', env: {LANG: 'C'},
 *                                  inheritEnv: true, stdin: '...', timeoutMs: 30000, outputLimit: 1048576});
 *   result.ok, result.outcome, result.exitCode, result.signal, result.stdout, result.stderr,
 *   result.stdoutTruncated, result.stderrTruncated, result.elapsedMs, result.error
 *
 * Spawning goes only through the host's PrkProcessRunner. A missing program or a timeout is returned,
 * not thrown. The process timeout is capped by the running test's own timeout. No shell is involved.
 */
#include <stdlib.h>
#include <string.h>

#include "prk_js_internal.h"

/** @brief C strings borrowed from JS values for one request; released together. */
typedef struct PrkJsProcessStrings {
    const char* program;
    const char* cwd;
    const char* standard_input;
    size_t standard_input_length;
    const char** arguments;
    size_t argument_count;
    char** environment; /**< malloc'd "NAME=VALUE" strings. */
    size_t environment_count;
} PrkJsProcessStrings;

static void prk_release_js_process_strings(JSContext* context, PrkJsProcessStrings* strings) {
    JS_FreeCString(context, strings->program);
    JS_FreeCString(context, strings->cwd);
    JS_FreeCString(context, strings->standard_input);
    for (size_t index = 0; strings->arguments != NULL && index < strings->argument_count; ++index) {
        JS_FreeCString(context, strings->arguments[index]);
    }
    free((void*)strings->arguments);
    for (size_t index = 0; strings->environment != NULL && index < strings->environment_count; ++index) {
        free(strings->environment[index]);
    }
    free(strings->environment);
    memset(strings, 0, sizeof *strings);
}

/** @brief Reads an optional string property. @return 0, or -1 with an exception pending. */
static int prk_read_js_string_property(JSContext* context, JSValueConst options, const char* key, const char** out_text,
                                       size_t* out_length) {
    JSValue value = JS_GetPropertyStr(context, options, key);
    int status = 0;
    if (JS_IsString(value)) {
        size_t length = 0;
        *out_text = JS_ToCStringLen(context, &length, value);
        status = *out_text != NULL ? 0 : -1;
        if (out_length != NULL) {
            *out_length = length;
        }
    } else if (!JS_IsUndefined(value) && !JS_IsException(value)) {
        JS_ThrowTypeError(context, "prk.runProcess: %s must be a string", key);
        status = -1;
    } else if (JS_IsException(value)) {
        status = -1;
    }
    JS_FreeValue(context, value);
    return status;
}

/** @brief Reads an optional positive integer property. @return 0, or -1 with an exception pending. */
static int prk_read_js_positive_property(JSContext* context, JSValueConst options, const char* key,
                                         uint64_t* out_value) {
    JSValue value = JS_GetPropertyStr(context, options, key);
    int status = 0;
    *out_value = 0;
    if (!JS_IsUndefined(value)) {
        double number = 0.0;
        if (!JS_IsNumber(value) || JS_ToFloat64(context, &number, value) != 0 || number < 1 ||
            number != (double)(int64_t)number || number > 9007199254740991.0) {
            JS_ThrowTypeError(context, "prk.runProcess: %s must be a positive integer", key);
            status = -1;
        } else {
            *out_value = (uint64_t)number;
        }
    }
    JS_FreeValue(context, value);
    return status;
}

/** @brief Reads options.args (an array of strings). @return 0, or -1 with an exception pending. */
static int prk_read_js_process_arguments(JSContext* context, JSValueConst options, PrkJsProcessStrings* strings) {
    JSValue args = JS_GetPropertyStr(context, options, "args");
    int64_t length = 0;
    int status = 0;
    if (JS_IsUndefined(args)) {
        status = 0;
    } else if (!JS_IsArray(args) || JS_GetLength(context, args, &length) != 0 || length > 100000) {
        JS_ThrowTypeError(context, "prk.runProcess: args must be an array of strings");
        status = -1;
    } else {
        strings->arguments = (const char**)calloc((size_t)length + 1, sizeof *strings->arguments);
        status = strings->arguments != NULL ? 0 : -1;
        if (status != 0) {
            JS_ThrowOutOfMemory(context);
        }
        for (int64_t index = 0; status == 0 && index < length; ++index) {
            JSValue item = JS_GetPropertyInt64(context, args, index);
            const char* const text = JS_IsString(item) ? JS_ToCString(context, item) : NULL;
            JS_FreeValue(context, item);
            if (text == NULL) {
                JS_ThrowTypeError(context, "prk.runProcess: args[%d] must be a string", (int)index);
                status = -1;
            } else {
                strings->arguments[strings->argument_count++] = text;
            }
        }
    }
    JS_FreeValue(context, args);
    return status;
}

/** @brief Formats one "NAME=VALUE" entry. @return 0, or -1 with an exception pending. */
static int prk_add_js_environment_entry(JSContext* context, JSValueConst env, JSAtom atom,
                                        PrkJsProcessStrings* strings) {
    const char* const name = JS_AtomToCString(context, atom);
    JSValue value = JS_GetProperty(context, env, atom);
    const char* const text = JS_IsString(value) ? JS_ToCString(context, value) : NULL;
    JS_FreeValue(context, value);
    int status = -1;
    if (name == NULL || text == NULL || name[0] == '\0' || strchr(name, '=') != NULL) {
        JS_ThrowTypeError(context, "prk.runProcess: env must map variable names (without '=') to strings");
    } else {
        const size_t size = strlen(name) + strlen(text) + 2;
        char* const entry = (char*)malloc(size);
        if (entry == NULL) {
            JS_ThrowOutOfMemory(context);
        } else {
            memcpy(entry, name, strlen(name));
            entry[strlen(name)] = '=';
            memcpy(entry + strlen(name) + 1, text, strlen(text) + 1);
            strings->environment[strings->environment_count++] = entry;
            status = 0;
        }
    }
    JS_FreeCString(context, text);
    JS_FreeCString(context, name);
    return status;
}

/** @brief Reads options.env (an object of strings). @return 0, or -1 with an exception pending. */
static int prk_read_js_process_environment(JSContext* context, JSValueConst options, PrkJsProcessStrings* strings) {
    JSValue env = JS_GetPropertyStr(context, options, "env");
    JSPropertyEnum* properties = NULL;
    uint32_t count = 0;
    int status = 0;
    if (JS_IsUndefined(env)) {
        status = 0;
    } else if (!JS_IsObject(env) || JS_IsArray(env) ||
               JS_GetOwnPropertyNames(context, &properties, &count, env, JS_GPN_STRING_MASK | JS_GPN_ENUM_ONLY) != 0) {
        if (!JS_HasException(context)) {
            JS_ThrowTypeError(context, "prk.runProcess: env must be an object of NAME: value strings");
        }
        status = -1;
    } else {
        strings->environment = (char**)calloc((size_t)count + 1, sizeof *strings->environment);
        status = strings->environment != NULL ? 0 : -1;
        if (status != 0) {
            JS_ThrowOutOfMemory(context);
        }
        for (uint32_t index = 0; status == 0 && index < count; ++index) {
            status = prk_add_js_environment_entry(context, env, properties[index].atom, strings);
        }
        JS_FreePropertyEnum(context, properties, count);
    }
    JS_FreeValue(context, env);
    return status;
}

/** @brief Rejects unknown option keys. @return 0, or -1 with an exception pending. */
static int prk_check_js_process_option_keys(JSContext* context, JSValueConst options) {
    static const char* const known[] = {"program", "args",      "cwd",         "env", "inheritEnv",
                                        "stdin",   "timeoutMs", "outputLimit", NULL};
    JSPropertyEnum* properties = NULL;
    uint32_t count = 0;
    if (JS_GetOwnPropertyNames(context, &properties, &count, options, JS_GPN_STRING_MASK | JS_GPN_ENUM_ONLY) != 0) {
        return -1;
    }
    int status = 0;
    for (uint32_t index = 0; status == 0 && index < count; ++index) {
        const char* const key = JS_AtomToCString(context, properties[index].atom);
        int is_known = 0;
        for (size_t known_index = 0; key != NULL && known[known_index] != NULL; ++known_index) {
            is_known |= strcmp(key, known[known_index]) == 0;
        }
        if (!is_known) {
            JS_ThrowTypeError(context, "prk.runProcess: unknown option '%s'", key != NULL ? key : "?");
            status = -1;
        }
        JS_FreeCString(context, key);
    }
    JS_FreePropertyEnum(context, properties, count);
    return status;
}

/** @brief Builds the JS result object. */
static JSValue prk_make_js_process_result(JSContext* context, const PrkProcessResult* result) {
    JSValue object = JS_NewObject(context);
    if (JS_IsException(object)) {
        return object;
    }
    JS_SetPropertyStr(context, object, "ok",
                      JS_NewBool(context, result->outcome == PRK_PROCESS_EXITED && result->exit_code == 0));
    JS_SetPropertyStr(context, object, "outcome", JS_NewString(context, prk_get_process_outcome_name(result->outcome)));
    if (result->outcome == PRK_PROCESS_EXITED) {
        JS_SetPropertyStr(context, object, "exitCode", JS_NewInt32(context, result->exit_code));
    }
    if (result->outcome == PRK_PROCESS_SIGNALED) {
        JS_SetPropertyStr(context, object, "signal", JS_NewInt32(context, result->termination_signal));
    }
    JS_SetPropertyStr(context, object, "stdout",
                      JS_NewStringLen(context, result->standard_output != NULL ? result->standard_output : "",
                                      result->standard_output != NULL ? result->standard_output_length : 0));
    JS_SetPropertyStr(context, object, "stderr",
                      JS_NewStringLen(context, result->standard_error != NULL ? result->standard_error : "",
                                      result->standard_error != NULL ? result->standard_error_length : 0));
    JS_SetPropertyStr(context, object, "stdoutTruncated", JS_NewBool(context, result->is_standard_output_truncated));
    JS_SetPropertyStr(context, object, "stderrTruncated", JS_NewBool(context, result->is_standard_error_truncated));
    JS_SetPropertyStr(context, object, "elapsedMs", JS_NewInt64(context, (int64_t)result->elapsed_ms));
    if (result->outcome == PRK_PROCESS_FAILED_TO_START) {
        JS_SetPropertyStr(context, object, "error", JS_NewString(context, result->error_message));
    }
    return object;
}

/** @brief Reads every option into @p strings and @p request. @return 0, or -1 with an exception pending. */
static int prk_read_js_process_request(JSContext* context, const PrkJsScript* script, JSValueConst options,
                                       PrkJsProcessStrings* strings, PrkProcessRequest* request) {
    uint64_t requested_timeout = 0;
    uint64_t output_limit = 0;
    if (prk_check_js_process_option_keys(context, options) != 0 ||
        prk_read_js_string_property(context, options, "program", &strings->program, NULL) != 0 ||
        prk_read_js_string_property(context, options, "cwd", &strings->cwd, NULL) != 0 ||
        prk_read_js_string_property(context, options, "stdin", &strings->standard_input,
                                    &strings->standard_input_length) != 0 ||
        prk_read_js_positive_property(context, options, "timeoutMs", &requested_timeout) != 0 ||
        prk_read_js_positive_property(context, options, "outputLimit", &output_limit) != 0 ||
        prk_read_js_process_arguments(context, options, strings) != 0 ||
        prk_read_js_process_environment(context, options, strings) != 0) {
        return -1;
    }
    if (strings->program == NULL || strings->program[0] == '\0') {
        JS_ThrowTypeError(context, "prk.runProcess: program must be a non-empty string");
        return -1;
    }
    JSValue inherit = JS_GetPropertyStr(context, options, "inheritEnv");
    request->is_environment_inherited = JS_IsUndefined(inherit) || JS_ToBool(context, inherit);
    JS_FreeValue(context, inherit);
    const uint64_t now = script->host.read_monotonic_milliseconds != NULL
                             ? script->host.read_monotonic_milliseconds(script->host.host_state)
                             : 0;
    request->timeout_ms = prk_cap_script_process_timeout_ms(requested_timeout, script->deadline_ms, now);
    if (request->timeout_ms > PRK_PROCESS_MAX_TIMEOUT_MS) {
        JS_ThrowRangeError(context, "prk.runProcess: timeoutMs must be at most 86400000");
        return -1;
    }
    request->executable = strings->program;
    request->working_directory = strings->cwd;
    request->standard_input = strings->standard_input;
    request->standard_input_length = strings->standard_input_length;
    request->arguments = strings->arguments;
    request->argument_count = strings->argument_count;
    request->environment = (const char* const*)strings->environment;
    request->environment_count = strings->environment_count;
    request->output_limit_bytes = (size_t)output_limit;
    return 0;
}

/** @brief prk.runProcess(options) -> result object. */
static JSValue prk_js_run_process(JSContext* context, JSValueConst this_value, int argc, JSValueConst* argv) {
    (void)this_value;
    const PrkJsScript* const script = prk_find_js_script(context);
    if (argc < 1 || !JS_IsObject(argv[0])) {
        return JS_ThrowTypeError(context, "prk.runProcess(options) needs an options object");
    }
    if (script->host.process_runner.spawn_process == NULL) {
        return JS_ThrowTypeError(context, "prk.runProcess: process spawn is not available in this host");
    }
    PrkJsProcessStrings strings;
    memset(&strings, 0, sizeof strings);
    PrkProcessRequest request;
    memset(&request, 0, sizeof request);
    if (prk_read_js_process_request(context, script, argv[0], &strings, &request) != 0) {
        prk_release_js_process_strings(context, &strings);
        return JS_EXCEPTION;
    }
    PrkProcessResult result;
    memset(&result, 0, sizeof result);
    const PrkStatus status =
        script->host.process_runner.spawn_process(script->host.process_runner.runner_state, &request, &result);
    prk_release_js_process_strings(context, &strings);
    if (status != PRK_STATUS_OK) {
        return JS_ThrowTypeError(context, "prk.runProcess: %s", prk_get_status_name(status));
    }
    JSValue object = prk_make_js_process_result(context, &result);
    prk_release_process_result(&result);
    return object;
}

int prk_add_js_process_api(JSContext* context, JSValueConst prk) {
    JSValue function = JS_NewCFunction(context, &prk_js_run_process, "runProcess", 1);
    if (JS_IsException(function)) {
        return -1;
    }
    return JS_SetPropertyStr(context, prk, "runProcess", function) < 0 ? -1 : 0;
}
