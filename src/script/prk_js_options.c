/**
 * @file prk_js_options.c
 * @brief Option readers shared by the JavaScript driver bindings (prk.compile, prk.assemble): unknown-key checks,
 *        string and string-list options, named choices, and werror / timeoutMs. Errors name the API function.
 */
#include <stdlib.h>
#include <string.h>

#include "prk_js_internal.h"

int prk_check_js_option_keys(JSContext* context, JSValueConst options, const char* const* known, const char* api) {
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
            JS_ThrowTypeError(context, "%s: unknown option '%s'", api, key != NULL ? key : "?");
            status = -1;
        }
        JS_FreeCString(context, key);
    }
    JS_FreePropertyEnum(context, properties, count);
    return status;
}

int prk_read_js_text_option(JSContext* context, JSValueConst options, const char* key, const char* api,
                            const char** out_text) {
    JSValue value = JS_GetPropertyStr(context, options, key);
    int status = JS_IsException(value) ? -1 : 0;
    if (JS_IsString(value)) {
        *out_text = JS_ToCString(context, value);
        status = *out_text != NULL ? 0 : -1;
    } else if (status == 0 && !JS_IsUndefined(value)) {
        JS_ThrowTypeError(context, "%s: %s must be a string", api, key);
        status = -1;
    }
    JS_FreeValue(context, value);
    return status;
}

int prk_read_js_list_option(JSContext* context, JSValueConst options, const char* key, const char* api,
                            PrkJsStringList* out_list) {
    JSValue value = JS_GetPropertyStr(context, options, key);
    int64_t length = 1;
    int status = 0;
    const int is_single = JS_IsString(value);
    if (JS_IsUndefined(value)) {
        length = 0;
    } else if (!is_single && (!JS_IsArray(value) || JS_GetLength(context, value, &length) != 0 || length > 100000)) {
        JS_ThrowTypeError(context, "%s: %s must be a string or an array of strings", api, key);
        status = -1;
    }
    if (status == 0 && length > 0) {
        out_list->items = (const char**)calloc((size_t)length, sizeof *out_list->items);
        status = out_list->items != NULL ? 0 : -1;
        if (status != 0) {
            JS_ThrowOutOfMemory(context);
        }
    }
    for (int64_t index = 0; status == 0 && index < length; ++index) {
        JSValue item = is_single ? JS_DupValue(context, value) : JS_GetPropertyInt64(context, value, index);
        const char* const text = JS_IsString(item) ? JS_ToCString(context, item) : NULL;
        JS_FreeValue(context, item);
        if (text == NULL) {
            JS_ThrowTypeError(context, "%s: %s[%d] must be a string", api, key, (int)index);
            status = -1;
        } else {
            out_list->items[out_list->count++] = text;
        }
    }
    JS_FreeValue(context, value);
    return status;
}

int prk_find_js_choice(JSContext* context, const char* key, const char* text, const char* const* names,
                       const char* api) {
    if (text == NULL) {
        return 0;
    }
    for (int index = 0; names[index] != NULL; ++index) {
        if (strcmp(text, names[index]) == 0) {
            return index;
        }
    }
    JS_ThrowTypeError(context, "%s: unknown %s '%s'", api, key, text);
    return -1;
}

int prk_read_js_werror_and_timeout(JSContext* context, const PrkJsScript* script, JSValueConst options, const char* api,
                                   int* out_is_warnings_as_errors, uint64_t* out_timeout_ms) {
    JSValue werror = JS_GetPropertyStr(context, options, "werror");
    *out_is_warnings_as_errors = JS_ToBool(context, werror) > 0;
    JS_FreeValue(context, werror);
    JSValue timeout = JS_GetPropertyStr(context, options, "timeoutMs");
    double requested = 0.0;
    int status = 0;
    if (!JS_IsUndefined(timeout) &&
        (!JS_IsNumber(timeout) || JS_ToFloat64(context, &requested, timeout) != 0 || requested < 1 ||
         requested != (double)(int64_t)requested || requested > (double)PRK_PROCESS_MAX_TIMEOUT_MS)) {
        JS_ThrowRangeError(context, "%s: timeoutMs must be a positive integer up to one day", api);
        status = -1;
    }
    JS_FreeValue(context, timeout);
    const uint64_t now = script->host.read_monotonic_milliseconds != NULL
                             ? script->host.read_monotonic_milliseconds(script->host.host_state)
                             : 0;
    *out_timeout_ms = prk_cap_script_process_timeout_ms((uint64_t)requested, script->deadline_ms, now);
    return status;
}

void prk_release_js_string_list(JSContext* context, PrkJsStringList* list) {
    for (size_t index = 0; index < list->count; ++index) {
        JS_FreeCString(context, list->items[index]);
    }
    free((void*)list->items);
    memset(list, 0, sizeof *list);
}
