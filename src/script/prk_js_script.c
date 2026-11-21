/**
 * @file prk_js_script.c
 * @brief Lifecycle of a sandboxed QuickJS-ng test script and the engine calls its test plan uses (#11).
 *
 * Implements include/proofrunkit/prk_js.h. Performs no IO: the caller passes script text in and
 * supplies print, clock, and file access through PrkScriptHost. QuickJS reports errors as return
 * values (no longjmp), so no control flow crosses the caller's C or C++ frames.
 */
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "prk_js_internal.h"

#define PRK_JS_STRINGIFY_VALUE(value) #value
#define PRK_JS_STRINGIFY(value) PRK_JS_STRINGIFY_VALUE(value)

const char* prk_get_js_engine_version_string(void) {
    return "QuickJS-ng " PRK_JS_STRINGIFY(QJS_VERSION_MAJOR) "." PRK_JS_STRINGIFY(
        QJS_VERSION_MINOR) "." PRK_JS_STRINGIFY(QJS_VERSION_PATCH) QJS_VERSION_SUFFIX;
}

PrkJsScript* prk_find_js_script(JSContext* context) {
    return (PrkJsScript*)JS_GetContextOpaque(context);
}

int prk_store_js_function(PrkJsScript* script, JSValueConst function, PrkScriptFunction* out_function) {
    if (script->function_count == script->function_capacity) {
        const size_t capacity = script->function_capacity == 0 ? 16 : script->function_capacity * 2;
        JSValue* const grown = (JSValue*)realloc(script->functions, capacity * sizeof(JSValue));
        if (grown == NULL) {
            return 0;
        }
        script->functions = grown;
        script->function_capacity = capacity;
    }
    script->functions[script->function_count] = JS_DupValue(script->context, function);
    *out_function = (PrkScriptFunction)script->function_count;
    script->function_count += 1;
    return 1;
}

/** @brief Interrupt handler: charges operations and stops the script past its budget or deadline. */
static int prk_interrupt_js_script(JSContext* context, void* opaque) {
    (void)context;
    PrkJsScript* const script = (PrkJsScript*)opaque;
    if (script->is_over_budget || script->is_past_deadline) {
        return 1; /* Sticky: a script that somehow continues is stopped again at the next check. */
    }
    script->operations_used += PRK_JS_OPERATIONS_PER_INTERRUPT;
    if (script->operations_used > script->instruction_limit) {
        script->is_over_budget = 1;
        return 1;
    }
    if (script->deadline_ms != 0 &&
        script->host.read_monotonic_milliseconds(script->host.host_state) >= script->deadline_ms) {
        script->is_past_deadline = 1;
        return 1;
    }
    return 0;
}

/** @brief Resets the operation budget and sets the deadline for one test (or the load, with 0). */
static void prk_start_js_budget(PrkJsScript* script, uint64_t timeout_ms) {
    script->operations_used = 0;
    script->is_over_budget = 0;
    script->is_past_deadline = 0;
    script->timeout_ms = timeout_ms;
    script->deadline_ms = 0;
    if (timeout_ms != 0 && script->host.read_monotonic_milliseconds != NULL) {
        const uint64_t now = script->host.read_monotonic_milliseconds(script->host.host_state);
        script->deadline_ms = now + timeout_ms < now ? UINT64_MAX : now + timeout_ms;
    }
}

/** @brief Copies @p text into @p out_text, truncating and always terminating. */
static void prk_copy_js_text(const char* text, char* out_text, size_t capacity) {
    if (out_text == NULL || capacity == 0) {
        return;
    }
    const size_t length = strlen(text);
    const size_t copied = length < capacity - 1 ? length : capacity - 1;
    memcpy(out_text, text, copied);
    out_text[copied] = '\0';
}

/** @brief Copies property @p name of @p object (converted to a string) into @p out_text. @return 1 when found. */
static int prk_copy_js_property_text(JSContext* context, JSValueConst object, const char* name, char* out_text,
                                     size_t capacity) {
    JSValue value = JS_GetPropertyStr(context, object, name);
    if (JS_IsException(value)) {
        JS_FreeValue(context, JS_GetException(context));
        return 0;
    }
    const char* const text = JS_IsUndefined(value) ? NULL : JS_ToCString(context, value);
    JS_FreeValue(context, value);
    if (text == NULL) {
        JS_FreeValue(context, JS_GetException(context));
        return 0;
    }
    prk_copy_js_text(text, out_text, capacity);
    JS_FreeCString(context, text);
    return 1;
}

/** @brief Reports whether the runtime is at its memory limit. */
static int prk_is_js_memory_exhausted(const PrkJsScript* script) {
    JSMemoryUsage usage;
    JS_ComputeMemoryUsage(script->runtime, &usage);
    return usage.malloc_size >= 0 && (size_t)usage.malloc_size + 64 * 1024 >= script->memory_limit;
}

/**
 * @brief Formats a SyntaxError whose stack is the single frame "    at file:line:column" as
 *        "file:line: SyntaxError: ...", the shape Lua uses for syntax errors. @return 1 when formatted.
 */
static int prk_format_js_syntax_error(const char* text, const char* stack, char* out_text, size_t capacity) {
    static const char frame_prefix[] = "    at ";
    if (strncmp(text, "SyntaxError", 11) != 0 || strncmp(stack, frame_prefix, sizeof frame_prefix - 1) != 0) {
        return 0;
    }
    const char* const location = stack + sizeof frame_prefix - 1;
    const char* const end = strchr(location, '\n') != NULL ? strchr(location, '\n') : location + strlen(location);
    const char* column = end;
    while (column > location && column[-1] != ':') {
        --column;
    }
    if (column <= location + 1 || strchr(location, '(') != NULL) {
        return 0; /* Not a bare "file:line:column" frame. */
    }
    (void)snprintf(out_text, capacity, "%.*s %s", (int)(column - location), location, text);
    return 1;
}

/**
 * @brief Describes an exception value and classifies it as a failure or a skip.
 * @return PRK_SCRIPT_CALL_SKIPPED for prk.skipTest(), otherwise PRK_SCRIPT_CALL_FAILED.
 */
static PrkScriptCallResult prk_describe_js_exception(PrkJsScript* script, JSValueConst exception, char* out_text,
                                                     size_t capacity) {
    JSContext* const context = script->context;
    char kind[16] = "";
    if (script->is_over_budget) {
        (void)snprintf(out_text, capacity, "instruction limit of %" PRIu64 " instructions exceeded",
                       script->instruction_limit);
        return PRK_SCRIPT_CALL_FAILED;
    }
    if (script->is_past_deadline) {
        (void)snprintf(out_text, capacity, "timeout of %" PRIu64 " ms exceeded", script->timeout_ms);
        return PRK_SCRIPT_CALL_FAILED;
    }
    if (JS_IsObject(exception) && prk_copy_js_property_text(context, exception, "__prkKind", kind, sizeof kind) &&
        (strcmp(kind, "failure") == 0 || strcmp(kind, "skip") == 0)) {
        if (!prk_copy_js_property_text(context, exception, "message", out_text, capacity)) {
            prk_copy_js_text("(error object without a message)", out_text, capacity);
        }
        return strcmp(kind, "skip") == 0 ? PRK_SCRIPT_CALL_SKIPPED : PRK_SCRIPT_CALL_FAILED;
    }
    char message[PRK_SCRIPT_ERROR_CAPACITY] = "";
    const int has_message =
        JS_IsError(exception) && prk_copy_js_property_text(context, exception, "message", message, sizeof message);
    if ((has_message && strcmp(message, "out of memory") == 0) ||
        (!JS_IsObject(exception) && prk_is_js_memory_exhausted(script))) {
        (void)snprintf(out_text, capacity, "memory limit of %zu bytes exceeded", script->memory_limit);
        return PRK_SCRIPT_CALL_FAILED;
    }
    const char* const text = JS_ToCString(context, exception);
    if (text == NULL) {
        JS_FreeValue(context, JS_GetException(context));
        prk_copy_js_text("unknown JavaScript error", out_text, capacity);
        return PRK_SCRIPT_CALL_FAILED;
    }
    char stack[PRK_SCRIPT_ERROR_CAPACITY] = "";
    if (JS_IsError(exception)) {
        (void)prk_copy_js_property_text(context, exception, "stack", stack, sizeof stack);
    }
    if (!prk_format_js_syntax_error(text, stack, out_text, capacity)) {
        (void)snprintf(out_text, capacity, "%s%s%s", text, stack[0] != '\0' ? "\n" : "", stack);
    }
    JS_FreeCString(context, text);
    return PRK_SCRIPT_CALL_FAILED;
}

/** @brief Runs queued promise jobs until none are left or the script is stopped. */
static void prk_run_js_pending_jobs(PrkJsScript* script) {
    JSContext* job_context = NULL;
    for (;;) {
        const int status = JS_ExecutePendingJob(script->runtime, &job_context);
        if (status == 0) {
            return;
        }
        if (status < 0) {
            JS_FreeValue(job_context, JS_GetException(job_context));
            if (script->is_over_budget || script->is_past_deadline) {
                return;
            }
        }
    }
}

/** @brief Turns the value a call returned (perhaps a promise) into a call result. Consumes @p value. */
static PrkScriptCallResult prk_settle_js_result(PrkJsScript* script, JSValue value, char* out_text, size_t capacity) {
    JSContext* const context = script->context;
    prk_run_js_pending_jobs(script);
    PrkScriptCallResult result = PRK_SCRIPT_CALL_OK;
    if (JS_IsPromise(value)) {
        const JSPromiseStateEnum state = JS_PromiseState(context, value);
        if (state == JS_PROMISE_REJECTED) {
            JSValue reason = JS_PromiseResult(context, value);
            result = prk_describe_js_exception(script, reason, out_text, capacity);
            JS_FreeValue(context, reason);
        } else if (state == JS_PROMISE_PENDING) {
            result = PRK_SCRIPT_CALL_FAILED;
            if (script->is_over_budget || script->is_past_deadline) {
                (void)prk_describe_js_exception(script, JS_UNDEFINED, out_text, capacity);
            } else {
                prk_copy_js_text("the returned promise never settled (scripts have no timers or IO to wait for)",
                                 out_text, capacity);
            }
        }
    }
    if (result == PRK_SCRIPT_CALL_OK && (script->is_over_budget || script->is_past_deadline)) {
        result = prk_describe_js_exception(script, JS_UNDEFINED, out_text, capacity);
    }
    JS_FreeValue(context, value);
    return result;
}

/** @brief PrkScriptEngineCalls.begin_test. */
static void prk_begin_js_test(void* engine, uint64_t timeout_ms) {
    prk_start_js_budget((PrkJsScript*)engine, timeout_ms);
}

/** @brief PrkScriptEngineCalls.call_function: calls a stored function; awaits a returned promise's jobs. */
static PrkScriptCallResult prk_call_js_function(void* engine, PrkScriptFunction function, char* out_message,
                                                size_t message_capacity) {
    PrkJsScript* const script = (PrkJsScript*)engine;
    JSContext* const context = script->context;
    if (function < 0 || (size_t)function >= script->function_count) {
        prk_copy_js_text("invalid function handle", out_message, message_capacity);
        return PRK_SCRIPT_CALL_FAILED;
    }
    JS_UpdateStackTop(script->runtime);
    script->is_running_test = 1;
    const JSValue returned = JS_Call(context, script->functions[function], JS_UNDEFINED, 0, NULL);
    PrkScriptCallResult result;
    if (JS_IsException(returned)) {
        JSValue exception = JS_GetException(context);
        result = prk_describe_js_exception(script, exception, out_message, message_capacity);
        JS_FreeValue(context, exception);
        prk_run_js_pending_jobs(script);
    } else {
        result = prk_settle_js_result(script, returned, out_message, message_capacity);
    }
    script->is_running_test = 0;
    if (result != PRK_SCRIPT_CALL_OK) {
        JS_RunGC(script->runtime); /* Reclaim what a failed call left behind. */
    }
    return result;
}

/** @brief PrkScriptEngineCalls.release_function. */
static void prk_release_js_function(void* engine, PrkScriptFunction function) {
    PrkJsScript* const script = (PrkJsScript*)engine;
    if (function >= 0 && (size_t)function < script->function_count) {
        JS_FreeValue(script->context, script->functions[function]);
        script->functions[function] = JS_UNDEFINED;
    }
}

PrkJsScript* prk_create_js_script(const PrkScriptLimits* limits, const PrkScriptHost* host,
                                  const PrkScriptRunSettings* settings) {
    PrkJsScript* const script = (PrkJsScript*)calloc(1, sizeof(PrkJsScript));
    if (script == NULL) {
        return NULL;
    }
    script->memory_limit = limits != NULL && limits->memory_limit_bytes != 0 ? limits->memory_limit_bytes
                                                                             : PRK_SCRIPT_DEFAULT_MEMORY_LIMIT_BYTES;
    script->instruction_limit = limits != NULL && limits->instruction_limit != 0 ? limits->instruction_limit
                                                                                 : PRK_SCRIPT_DEFAULT_INSTRUCTION_LIMIT;
    if (host != NULL) {
        script->host = *host;
    }
    script->registration_status = PRK_STATUS_OK;
    script->runtime = JS_NewRuntime();
    if (script->runtime == NULL) {
        free(script);
        return NULL;
    }
    JS_SetMemoryLimit(script->runtime, script->memory_limit);
    JS_SetMaxStackSize(script->runtime, PRK_JS_STACK_LIMIT_BYTES);
    JS_SetCanBlock(script->runtime, false);
    JS_SetInterruptHandler(script->runtime, &prk_interrupt_js_script, script);
    script->context = JS_NewContext(script->runtime);
    const PrkScriptEngineCalls engine_calls = {&prk_begin_js_test, &prk_call_js_function, &prk_release_js_function,
                                               script};
    script->plan = prk_create_test_plan(&engine_calls, settings);
    if (script->context == NULL || script->plan == NULL) {
        prk_destroy_js_script(script);
        return NULL;
    }
    JS_SetContextOpaque(script->context, script);
    prk_start_js_budget(script, 0);
    if (prk_open_js_test_api(script->context) != 0) {
        prk_destroy_js_script(script);
        return NULL;
    }
    return script;
}

void prk_destroy_js_script(PrkJsScript* script) {
    if (script == NULL) {
        return;
    }
    prk_destroy_test_plan(script->plan); /* Releases function slots, so it runs before the context goes. */
    for (size_t index = 0; index < script->function_count; ++index) {
        JS_FreeValue(script->context, script->functions[index]);
    }
    free(script->functions);
    if (script->context != NULL) {
        JS_FreeContext(script->context);
    }
    JS_FreeRuntime(script->runtime);
    free(script);
}

PrkStatus prk_load_js_test_script(PrkJsScript* script, PrkRegistry* registry, const char* chunk_name,
                                  const char* source, size_t source_length, char* out_error, size_t error_capacity) {
    if (script == NULL || registry == NULL || chunk_name == NULL || source == NULL || script->is_loaded) {
        return PRK_STATUS_INVALID_ARGUMENT;
    }
    script->is_loaded = 1;
    char* const text = (char*)malloc(source_length + 1); /* QuickJS needs a NUL after the source. */
    if (text == NULL) {
        return PRK_STATUS_OUT_OF_MEMORY;
    }
    memcpy(text, source, source_length);
    text[source_length] = '\0';
    prk_start_js_budget(script, 0);
    JS_UpdateStackTop(script->runtime);
    script->loading_registry = registry;
    const JSValue returned = JS_Eval(script->context, text, source_length, chunk_name, JS_EVAL_TYPE_GLOBAL);
    free(text);
    PrkScriptCallResult result;
    char message[PRK_SCRIPT_ERROR_CAPACITY] = "";
    if (JS_IsException(returned)) {
        JSValue exception = JS_GetException(script->context);
        result = prk_describe_js_exception(script, exception, message, sizeof message);
        JS_FreeValue(script->context, exception);
    } else {
        result = prk_settle_js_result(script, returned, message, sizeof message);
    }
    script->loading_registry = NULL;
    if (result == PRK_SCRIPT_CALL_OK && prk_count_open_test_suites(script->plan) != 0) {
        result = PRK_SCRIPT_CALL_FAILED;
        prk_copy_js_text("a prk.describe() suite was left open", message, sizeof message);
    }
    if (result == PRK_SCRIPT_CALL_OK) {
        return PRK_STATUS_OK;
    }
    prk_copy_js_text(message, out_error, error_capacity);
    return script->registration_status != PRK_STATUS_OK ? script->registration_status : PRK_STATUS_SCRIPT_ERROR;
}

size_t prk_count_js_script_tests(const PrkJsScript* script) {
    return script == NULL ? 0 : prk_count_planned_tests(script->plan);
}

size_t prk_get_js_script_memory_in_use(const PrkJsScript* script) {
    if (script == NULL) {
        return 0;
    }
    JSMemoryUsage usage;
    JS_ComputeMemoryUsage(script->runtime, &usage);
    return usage.malloc_size < 0 ? 0 : (size_t)usage.malloc_size;
}
