/**
 * @file prk_js_api.c
 * @brief The script-facing global `prk` object for JavaScript test scripts (#11), the camelCase twin
 *        of the Lua `prk` table (#12). See docs/js-tests.md.
 *
 * Registration functions, the clock, and file access are C functions over the engine-neutral test
 * plan and PrkScriptHost; assertions, failTest, and skipTest are a JavaScript prelude (below) that
 * throws AssertionError / Skip objects, so tests can catch them with try/catch. prk.runProcess lives in
 * prk_js_process.c.
 *
 * Size: over the 400-line review mark because about 180 lines are the JavaScript prelude, a data table
 * kept beside the C functions it calls.
 */
#include <stdlib.h>
#include <string.h>

#include "prk_js_internal.h"

/** @brief Most tags one test or suite may carry. */
#define PRK_JS_MAX_TAGS 32

/* ------------------------------------------------------------------ helpers */

/** @brief Throws unless the top-level script is loading. @return 1 when allowed. */
static int prk_require_loading_js_script(JSContext* context, const char* function_name) {
    const PrkJsScript* const script = prk_find_js_script(context);
    if (script->loading_registry == NULL || script->is_running_test) {
        JS_ThrowTypeError(context, "%s can only be called while the script loads, not inside a test", function_name);
        return 0;
    }
    return 1;
}

/** @brief Options read from an addTest/describe options object. Strings are owned (free with release). */
typedef struct PrkJsTestOptions {
    const char* tags[PRK_JS_MAX_TAGS];
    size_t tag_count;
    uint64_t timeout_ms;
    const char* skip_reason;
    int is_skip_reason_owned;
} PrkJsTestOptions;

static void prk_release_js_test_options(JSContext* context, PrkJsTestOptions* options) {
    for (size_t index = 0; index < options->tag_count; ++index) {
        JS_FreeCString(context, options->tags[index]);
    }
    options->tag_count = 0;
    if (options->is_skip_reason_owned) {
        JS_FreeCString(context, options->skip_reason);
    }
    options->skip_reason = NULL;
    options->is_skip_reason_owned = 0;
}

/** @brief Reads options.tags (an array of non-empty strings). @return 0, or -1 with an exception pending. */
static int prk_read_js_tags(JSContext* context, JSValueConst tags, const char* function_name, PrkJsTestOptions* out) {
    int64_t length = 0;
    if (!JS_IsArray(tags) || JS_GetLength(context, tags, &length) != 0) {
        JS_ThrowTypeError(context, "%s: tags must be an array of strings", function_name);
        return -1;
    }
    if (length > PRK_JS_MAX_TAGS) {
        JS_ThrowTypeError(context, "%s: at most %d tags are allowed", function_name, PRK_JS_MAX_TAGS);
        return -1;
    }
    for (int64_t index = 0; index < length; ++index) {
        JSValue tag = JS_GetPropertyInt64(context, tags, index);
        const char* const text = JS_IsString(tag) ? JS_ToCString(context, tag) : NULL;
        JS_FreeValue(context, tag);
        if (text == NULL || text[0] == '\0') {
            if (text != NULL) {
                JS_FreeCString(context, text);
            }
            JS_ThrowTypeError(context, "%s: tags must be non-empty strings", function_name);
            return -1;
        }
        out->tags[out->tag_count] = text;
        out->tag_count += 1;
    }
    return 0;
}

/** @brief Reads one known option. @return 0, or -1 with an exception pending. */
static int prk_read_js_option(JSContext* context, const char* key, JSValueConst value, const char* function_name,
                              int allow_test_fields, PrkJsTestOptions* out) {
    if (strcmp(key, "tags") == 0) {
        return prk_read_js_tags(context, value, function_name, out);
    }
    if (allow_test_fields && strcmp(key, "timeoutMs") == 0) {
        int64_t timeout = 0;
        double number = 0.0;
        if (!JS_IsNumber(value) || JS_ToFloat64(context, &number, value) != 0 || number != (double)(int64_t)number ||
            JS_ToInt64(context, &timeout, value) != 0 || timeout <= 0) {
            JS_ThrowTypeError(context, "%s: timeoutMs must be a positive integer", function_name);
            return -1;
        }
        out->timeout_ms = (uint64_t)timeout;
        return 0;
    }
    if (allow_test_fields && strcmp(key, "skip") == 0) {
        if (JS_IsString(value)) {
            out->skip_reason = JS_ToCString(context, value);
            out->is_skip_reason_owned = out->skip_reason != NULL;
            return out->skip_reason != NULL ? 0 : -1;
        }
        out->skip_reason = JS_ToBool(context, value) ? "skipped" : NULL;
        return 0;
    }
    JS_ThrowTypeError(context, "%s: unknown option '%s'", function_name, key);
    return -1;
}

/** @brief Parses an options object (undefined allowed). @return 0, or -1 with an exception pending. */
static int prk_read_js_test_options(JSContext* context, JSValueConst options, const char* function_name,
                                    int allow_test_fields, PrkJsTestOptions* out) {
    memset(out, 0, sizeof *out);
    if (JS_IsUndefined(options) || JS_IsNull(options)) {
        return 0;
    }
    if (!JS_IsObject(options)) {
        JS_ThrowTypeError(context, "%s: options must be an object", function_name);
        return -1;
    }
    JSPropertyEnum* properties = NULL;
    uint32_t property_count = 0;
    if (JS_GetOwnPropertyNames(context, &properties, &property_count, options, JS_GPN_STRING_MASK | JS_GPN_ENUM_ONLY) !=
        0) {
        return -1;
    }
    int status = 0;
    for (uint32_t index = 0; index < property_count && status == 0; ++index) {
        const char* const key = JS_AtomToCString(context, properties[index].atom);
        JSValue value = JS_GetProperty(context, options, properties[index].atom);
        status = key == NULL || JS_IsException(value)
                     ? -1
                     : prk_read_js_option(context, key, value, function_name, allow_test_fields, out);
        JS_FreeValue(context, value);
        JS_FreeCString(context, key);
    }
    JS_FreePropertyEnum(context, properties, property_count);
    if (status != 0) {
        prk_release_js_test_options(context, out);
    }
    return status;
}

/* ------------------------------------------------------------------ registration */

/** @brief prk.addTest(name, body [, options]). */
static JSValue prk_js_add_test(JSContext* context, JSValueConst this_value, int argc, JSValueConst* argv) {
    (void)this_value;
    PrkJsScript* const script = prk_find_js_script(context);
    if (argc < 2 || !JS_IsString(argv[0]) || !JS_IsFunction(context, argv[1])) {
        return JS_ThrowTypeError(context, "prk.addTest(name, body [, options]) needs a name string and a function");
    }
    if (!prk_require_loading_js_script(context, "prk.addTest")) {
        return JS_EXCEPTION;
    }
    const char* const name = JS_ToCString(context, argv[0]);
    if (name == NULL) {
        return JS_EXCEPTION;
    }
    PrkJsTestOptions options;
    PrkScriptFunction body = 0;
    if (name[0] == '\0' ||
        prk_read_js_test_options(context, argc > 2 ? argv[2] : JS_UNDEFINED, "prk.addTest", 1, &options) != 0) {
        if (name[0] == '\0') {
            JS_ThrowTypeError(context, "prk.addTest: test name must not be empty");
        }
        JS_FreeCString(context, name);
        return JS_EXCEPTION;
    }
    if (!prk_store_js_function(script, argv[1], &body)) {
        prk_release_js_test_options(context, &options);
        JS_FreeCString(context, name);
        return JS_ThrowOutOfMemory(context);
    }
    const PrkPlannedTestOptions planned = {
        name, body, options.tags, options.tag_count, options.timeout_ms, options.skip_reason};
    const PrkStatus status = prk_add_planned_test(script->plan, script->loading_registry, &planned, NULL);
    prk_release_js_test_options(context, &options);
    JSValue result = JS_UNDEFINED;
    if (status != PRK_STATUS_OK) {
        script->registration_status = status;
        result = JS_ThrowTypeError(context, "could not register test '%s': %s", name, prk_get_status_name(status));
    }
    JS_FreeCString(context, name);
    return result;
}

/** @brief prk.describe(name, define [, options]). */
static JSValue prk_js_describe(JSContext* context, JSValueConst this_value, int argc, JSValueConst* argv) {
    (void)this_value;
    PrkJsScript* const script = prk_find_js_script(context);
    if (argc < 2 || !JS_IsString(argv[0]) || !JS_IsFunction(context, argv[1])) {
        return JS_ThrowTypeError(context, "prk.describe(name, define [, options]) needs a name string and a function");
    }
    if (!prk_require_loading_js_script(context, "prk.describe")) {
        return JS_EXCEPTION;
    }
    const char* const name = JS_ToCString(context, argv[0]);
    if (name == NULL) {
        return JS_EXCEPTION;
    }
    PrkJsTestOptions options;
    if (name[0] == '\0' || strchr(name, '.') != NULL) {
        JS_FreeCString(context, name);
        return JS_ThrowTypeError(context, "prk.describe: suite name must be non-empty without '.'");
    }
    if (prk_read_js_test_options(context, argc > 2 ? argv[2] : JS_UNDEFINED, "prk.describe", 0, &options) != 0) {
        JS_FreeCString(context, name);
        return JS_EXCEPTION;
    }
    const PrkStatus status = prk_begin_test_suite(script->plan, name, options.tags, options.tag_count);
    prk_release_js_test_options(context, &options);
    JS_FreeCString(context, name);
    if (status != PRK_STATUS_OK) {
        return JS_ThrowTypeError(context, "prk.describe: could not open the suite: %s (suites nest at most 31 deep)",
                                 prk_get_status_name(status));
    }
    JSValue defined = JS_Call(context, argv[1], JS_UNDEFINED, 0, NULL);
    if (JS_IsException(defined)) {
        return JS_EXCEPTION; /* The load fails, so the open suite does not matter. */
    }
    JS_FreeValue(context, defined);
    (void)prk_end_test_suite(script->plan);
    return JS_UNDEFINED;
}

/** @brief prk.beforeAll / afterAll / beforeEach / afterEach(hook); @p magic is the PrkSuiteHookKind. */
static JSValue prk_js_add_suite_hook(JSContext* context, JSValueConst this_value, int argc, JSValueConst* argv,
                                     int magic) {
    (void)this_value;
    static const char* const names[PRK_HOOK_KIND_COUNT] = {"prk.beforeAll", "prk.afterAll", "prk.beforeEach",
                                                           "prk.afterEach"};
    const char* const function_name = magic >= 0 && magic < PRK_HOOK_KIND_COUNT ? names[magic] : "prk hook";
    PrkJsScript* const script = prk_find_js_script(context);
    if (argc < 1 || !JS_IsFunction(context, argv[0])) {
        return JS_ThrowTypeError(context, "%s(hook) needs a function", function_name);
    }
    if (!prk_require_loading_js_script(context, function_name)) {
        return JS_EXCEPTION;
    }
    PrkScriptFunction hook = 0;
    if (!prk_store_js_function(script, argv[0], &hook)) {
        return JS_ThrowOutOfMemory(context);
    }
    const PrkStatus status = prk_add_suite_hook(script->plan, (PrkSuiteHookKind)magic, hook);
    return status == PRK_STATUS_OK ? JS_UNDEFINED
                                   : JS_ThrowTypeError(context, "%s: %s", function_name, prk_get_status_name(status));
}

/* ------------------------------------------------------------------ host services */

/** @brief print(...) and console.log(...): space-separated values and a newline to the host. */
static JSValue prk_js_print(JSContext* context, JSValueConst this_value, int argc, JSValueConst* argv) {
    (void)this_value;
    const PrkJsScript* const script = prk_find_js_script(context);
    for (int index = 0; index < argc; ++index) {
        size_t length = 0;
        const char* const text = JS_ToCStringLen(context, &length, argv[index]);
        if (text == NULL) {
            return JS_EXCEPTION;
        }
        if (script->host.write_text != NULL) {
            if (index > 0) {
                script->host.write_text(script->host.host_state, " ", 1);
            }
            script->host.write_text(script->host.host_state, text, length);
        }
        JS_FreeCString(context, text);
    }
    if (script->host.write_text != NULL) {
        script->host.write_text(script->host.host_state, "\n", 1);
    }
    return JS_UNDEFINED;
}

/** @brief prk.readClockMs(). */
static JSValue prk_js_read_clock_ms(JSContext* context, JSValueConst this_value, int argc, JSValueConst* argv) {
    (void)this_value;
    (void)argc;
    (void)argv;
    const PrkJsScript* const script = prk_find_js_script(context);
    if (script->host.read_monotonic_milliseconds == NULL) {
        return JS_ThrowTypeError(context, "prk.readClockMs: no clock is available in this host");
    }
    return JS_NewInt64(context, (int64_t)script->host.read_monotonic_milliseconds(script->host.host_state));
}

/** @brief Throws the error for a failed file request. */
static JSValue prk_throw_js_file_error(JSContext* context, const char* function_name, const char* path,
                                       PrkFileAccessStatus status) {
    if (status == PRK_FILE_ACCESS_DENIED) {
        return JS_ThrowTypeError(context,
                                 "%s: access denied for '%s' (only relative paths inside the working directory)",
                                 function_name, path);
    }
    return JS_ThrowTypeError(context, "%s: '%s': %s", function_name, path, prk_get_file_access_status_name(status));
}

/** @brief prk.isFilePresent(path) -> boolean. */
static JSValue prk_js_is_file_present(JSContext* context, JSValueConst this_value, int argc, JSValueConst* argv) {
    (void)this_value;
    const PrkJsScript* const script = prk_find_js_script(context);
    if (script->host.find_file == NULL) {
        return JS_ThrowTypeError(context, "prk.isFilePresent: file access is not available in this host");
    }
    const char* const path = argc > 0 && JS_IsString(argv[0]) ? JS_ToCString(context, argv[0]) : NULL;
    if (path == NULL) {
        return JS_ThrowTypeError(context, "prk.isFilePresent(path) needs a path string");
    }
    const PrkFileAccessStatus status = script->host.find_file(script->host.host_state, path);
    JSValue result = status == PRK_FILE_ACCESS_OK || status == PRK_FILE_ACCESS_NOT_FOUND
                         ? JS_NewBool(context, status == PRK_FILE_ACCESS_OK)
                         : prk_throw_js_file_error(context, "prk.isFilePresent", path, status);
    JS_FreeCString(context, path);
    return result;
}

/** @brief Bytes collected from the host for prk.readTextFile. */
typedef struct PrkJsFileBuffer {
    char* data;
    size_t length;
    size_t capacity;
    size_t limit;
    int is_over_limit;
} PrkJsFileBuffer;

/** @brief PrkReceiveFileBytes sink for prk.readTextFile. */
static void prk_receive_js_file_bytes(void* sink, const char* bytes, size_t length) {
    PrkJsFileBuffer* const buffer = (PrkJsFileBuffer*)sink;
    if (buffer->is_over_limit || length == 0) {
        return;
    }
    if (length > buffer->limit - buffer->length) {
        buffer->is_over_limit = 1;
        return;
    }
    if (buffer->length + length > buffer->capacity) {
        size_t capacity = buffer->capacity == 0 ? 4096 : buffer->capacity;
        while (capacity < buffer->length + length) {
            capacity = capacity > buffer->limit / 2 ? buffer->limit : capacity * 2;
        }
        char* const grown = (char*)realloc(buffer->data, capacity);
        if (grown == NULL) {
            buffer->is_over_limit = 1;
            return;
        }
        buffer->data = grown;
        buffer->capacity = capacity;
    }
    memcpy(buffer->data + buffer->length, bytes, length);
    buffer->length += length;
}

/** @brief prk.readTextFile(path) -> string (the file decoded as UTF-8). */
static JSValue prk_js_read_text_file(JSContext* context, JSValueConst this_value, int argc, JSValueConst* argv) {
    (void)this_value;
    const PrkJsScript* const script = prk_find_js_script(context);
    if (script->host.read_file == NULL) {
        return JS_ThrowTypeError(context, "prk.readTextFile: file access is not available in this host");
    }
    const char* const path = argc > 0 && JS_IsString(argv[0]) ? JS_ToCString(context, argv[0]) : NULL;
    if (path == NULL) {
        return JS_ThrowTypeError(context, "prk.readTextFile(path) needs a path string");
    }
    const size_t in_use = prk_get_js_script_memory_in_use(script);
    PrkJsFileBuffer buffer = {NULL, 0, 0, script->memory_limit > in_use ? script->memory_limit - in_use : 0, 0};
    const PrkFileAccessStatus status =
        script->host.read_file(script->host.host_state, path, &prk_receive_js_file_bytes, &buffer);
    JSValue result;
    if (status != PRK_FILE_ACCESS_OK) {
        result = prk_throw_js_file_error(context, "prk.readTextFile", path, status);
    } else if (buffer.is_over_limit) {
        result = JS_ThrowTypeError(context, "prk.readTextFile: '%s' does not fit in the script memory limit", path);
    } else {
        result = JS_NewStringLen(context, buffer.data != NULL ? buffer.data : "", buffer.length);
    }
    free(buffer.data);
    JS_FreeCString(context, path);
    return result;
}

/* ------------------------------------------------------------------ prelude */

/* JavaScript source of failTest, skipTest, the assertions, and measureElapsedMs. It is evaluated as
   one function expression and called with the prk object. Keep in sync with docs/js-tests.md. */
static const char* const prk_js_prelude[] = {
    "(function (prk) {\n"
    "'use strict';\n"
    "class AssertionError extends Error {\n"
    "  constructor(message) { super(message); this.name = 'AssertionError'; this.__prkKind = 'failure'; }\n"
    "  toString() { return this.message; }\n"
    "}\n"
    "class Skip extends Error {\n"
    "  constructor(reason) { super(reason); this.name = 'Skip'; this.__prkKind = 'skip'; }\n"
    "  toString() { return this.message; }\n"
    "}\n"
    "function where(depth) {\n"
    "  const lines = String(new Error().stack).split('\\n');\n"
    "  const line = lines[depth] || '';\n"
    "  const match = /\\((.*):(\\d+):\\d+\\)\\s*$/.exec(line) || /at (.*):(\\d+):\\d+\\s*$/.exec(line);\n"
    "  return match ? match[1] + ':' + match[2] + ': ' : '';\n"
    "}\n"
    "function fail(message) { throw new AssertionError(where(3) + message); }\n"
    "function detail(message, fallback, text) { return (message === undefined ? fallback : message) + ': ' + text; }\n"
    "function show(value) {\n"
    "  if (typeof value === 'string') return JSON.stringify(value);\n"
    "  if (typeof value === 'bigint') return String(value) + 'n';\n"
    "  if (Array.isArray(value) || (value !== null && typeof value === 'object' &&\n"
    "      Object.getPrototypeOf(value) === Object.prototype)) {\n"
    "    try { return JSON.stringify(value); } catch (problem) { return String(value); }\n"
    "  }\n"
    "  return String(value);\n"
    "}\n"
    "function requireNumber(value, functionName, what) {\n"
    "  if (typeof value !== 'number') throw new TypeError(functionName + ': ' + what + ' must be a number');\n"
    "}\n"
    "prk.failTest = function failTest(message) {\n"
    "  throw new AssertionError(where(2) + (message === undefined ? 'test failed' : String(message)));\n"
    "};\n"
    "prk.skipTest = function skipTest(reason) { throw new Skip(reason === undefined ? 'skipped' : String(reason)); };\n"
    "prk.expectTrue = function expectTrue(value, message) {\n"
    "  if (!value) fail(message === undefined ? 'expected a truthy value, got ' + show(value) : message);\n"
    "};\n"
    "prk.expectFalse = function expectFalse(value, message) {\n"
    "  if (value) fail(message === undefined ? 'expected a falsy value, got ' + show(value) : message);\n"
    "};\n"
    "prk.expectEqual = function expectEqual(actual, expected, message) {\n"
    "  if (actual !== expected) fail(detail(message, 'values differ', 'expected ' + show(expected) + ', got ' +\n"
    "    show(actual)));\n"
    "};\n"
    "prk.expectNotEqual = function expectNotEqual(actual, unexpected, message) {\n"
    "  if (actual === unexpected) fail(detail(message, 'values are equal', 'both are ' + show(actual)));\n"
    "};\n"
    "prk.expectNullish = function expectNullish(value, message) {\n"
    "  if (value !== null && value !== undefined) fail(detail(message, 'expected null or undefined', 'got ' +\n"
    "    show(value)));\n"
    "};\n"
    "prk.expectNotNullish = function expectNotNullish(value, message) {\n"
    "  if (value === null || value === undefined) fail(message === undefined ? 'expected a value, got ' +\n"
    "    show(value) : message);\n"
    "};\n",

    "prk.expectNear = function expectNear(actual, expected, tolerance, message) {\n"
    "  requireNumber(actual, 'prk.expectNear', 'actual');\n"
    "  requireNumber(expected, 'prk.expectNear', 'expected');\n"
    "  requireNumber(tolerance, 'prk.expectNear', 'tolerance');\n"
    "  if (!(Math.abs(actual - expected) <= tolerance)) fail(detail(message, 'values are not near', 'expected ' +\n"
    "    show(expected) + ' +/- ' + show(tolerance) + ', got ' + show(actual)));\n"
    "};\n"
    "prk.expectLessThan = function expectLessThan(actual, limit, message) {\n"
    "  if (!(actual < limit)) fail(detail(message, 'value too large', 'expected ' + show(actual) + ' < ' +\n"
    "    show(limit)));\n"
    "};\n"
    "prk.expectLessOrEqual = function expectLessOrEqual(actual, limit, message) {\n"
    "  if (!(actual <= limit)) fail(detail(message, 'value too large', 'expected ' + show(actual) + ' <= ' +\n"
    "    show(limit)));\n"
    "};\n"
    "prk.expectGreaterThan = function expectGreaterThan(actual, limit, message) {\n"
    "  if (!(actual > limit)) fail(detail(message, 'value too small', 'expected ' + show(actual) + ' > ' +\n"
    "    show(limit)));\n"
    "};\n"
    "prk.expectGreaterOrEqual = function expectGreaterOrEqual(actual, limit, message) {\n"
    "  if (!(actual >= limit)) fail(detail(message, 'value too small', 'expected ' + show(actual) + ' >= ' +\n"
    "    show(limit)));\n"
    "};\n"
    "prk.expectContains = function expectContains(container, item, message) {\n"
    "  let found;\n"
    "  if (typeof container === 'string') found = typeof item === 'string' && container.includes(item);\n"
    "  else if (Array.isArray(container)) found = container.includes(item);\n"
    "  else if (container instanceof Set) found = container.has(item);\n"
    "  else if (container !== null && typeof container === 'object') found = Object.values(container).includes(item);\n"
    "  else throw new TypeError('prk.expectContains: container must be a string, an array, a Set, or an object');\n"
    "  if (!found) fail(detail(message, 'item not found', 'expected ' + show(container) + ' to contain ' +\n"
    "    show(item)));\n"
    "};\n"
    "prk.expectMatch = function expectMatch(text, pattern, message) {\n"
    "  const regexp = pattern instanceof RegExp ? pattern : new RegExp(pattern);\n"
    "  if (typeof text !== 'string' || !regexp.test(text)) fail(detail(message, 'no match', 'expected ' +\n"
    "    show(text) + ' to match ' + String(regexp)));\n"
    "};\n"
    "prk.expectType = function expectType(value, typeName, message) {\n"
    "  if (typeof value !== typeName) fail(detail(message, 'wrong type', 'expected ' + typeName + ', got ' +\n"
    "    typeof value));\n"
    "};\n",

    "function keyText(key) { return typeof key === 'number' ? '[' + key + ']' : '[' + JSON.stringify(key) + ']'; }\n"
    "function findDifference(actual, expected, path, seen) {\n"
    "  if (Object.is(actual, expected)) return null;\n"
    "  if (actual === null || expected === null || typeof actual !== 'object' || typeof expected !== 'object') {\n"
    "    return 'at ' + path + ': expected ' + show(expected) + ', got ' + show(actual);\n"
    "  }\n"
    "  if (seen.get(actual) === expected) return null;\n"
    "  seen.set(actual, expected);\n"
    "  if (Array.isArray(actual) !== Array.isArray(expected)) {\n"
    "    return 'at ' + path + ': expected ' + (Array.isArray(expected) ? 'an array' : 'an object') + ', got ' +\n"
    "      (Array.isArray(actual) ? 'an array' : 'an object');\n"
    "  }\n"
    "  const isArray = Array.isArray(expected);\n"
    "  for (const key of Object.keys(expected)) {\n"
    "    const name = isArray ? Number(key) : key;\n"
    "    const difference = findDifference(actual[key], expected[key], path + keyText(name), seen);\n"
    "    if (difference) return difference;\n"
    "  }\n"
    "  for (const key of Object.keys(actual)) {\n"
    "    if (!Object.prototype.hasOwnProperty.call(expected, key)) {\n"
    "      return 'at ' + path + keyText(isArray ? Number(key) : key) + ': unexpected ' + show(actual[key]);\n"
    "    }\n"
    "  }\n"
    "  return null;\n"
    "}\n"
    "prk.expectDeepEqual = function expectDeepEqual(actual, expected, message) {\n"
    "  const difference = findDifference(actual, expected, 'value', new Map());\n"
    "  if (difference) fail(detail(message, 'values differ', difference));\n"
    "};\n"
    "prk.expectError = function expectError(action, pattern, message) {\n"
    "  if (typeof action !== 'function') throw new TypeError('prk.expectError: action must be a function');\n"
    "  let problem;\n"
    "  let threw = false;\n"
    "  try { action(); } catch (caught) { threw = true; problem = caught; }\n"
    "  if (!threw) fail(message === undefined ? 'expected an error, but the function returned normally' : message);\n"
    "  const text = problem instanceof Error && !(problem instanceof AssertionError) && !(problem instanceof Skip)\n"
    "    ? String(problem.message) : String(problem);\n"
    "  if (pattern !== undefined) {\n"
    "    const matches = pattern instanceof RegExp ? pattern.test(text) : text.includes(String(pattern));\n"
    "    if (!matches) fail(detail(message, 'wrong error', 'expected an error matching ' + show(String(pattern)) +\n"
    "      ', got ' + show(text)));\n"
    "  }\n"
    "  return text;\n"
    "};\n"
    "prk.expectFilePresent = function expectFilePresent(path, message) {\n"
    "  if (!prk.isFilePresent(path)) fail(message === undefined ? 'expected file ' + show(path) + ' to exist' : "
    "message);\n"
    "};\n"
    "prk.expectFileContains = function expectFileContains(path, text, message) {\n"
    "  if (!prk.readTextFile(path).includes(text)) fail(detail(message, 'text not found', 'expected file ' +\n"
    "    show(path) + ' to contain ' + show(text)));\n"
    "};\n"
    "function describeProcess(result) {\n"
    "  let text;\n"
    "  if (result.outcome === 'exited') text = 'exit code ' + result.exitCode;\n"
    "  else if (result.outcome === 'failed to start') text = 'failed to start: ' + result.error;\n"
    "  else if (result.outcome === 'timed out') text = 'timed out after ' + result.elapsedMs + ' ms';\n"
    "  else text = result.outcome + ' ' + result.signal;\n"
    "  if (result.stderr) text += '\\nstderr: ' + result.stderr.slice(0, 2000);\n"
    "  return text;\n"
    "}\n"
    "prk.expectExitCode = function expectExitCode(result, expected, message) {\n"
    "  if (result === null || typeof result !== 'object' || result.outcome === undefined) {\n"
    "    throw new TypeError('prk.expectExitCode: result must come from prk.runProcess');\n"
    "  }\n"
    "  if (result.outcome !== 'exited' || result.exitCode !== expected) fail(detail(message,\n"
    "    'unexpected process result', 'expected exit code ' + expected + ', got ' + describeProcess(result)));\n"
    "};\n"
    "prk.measureElapsedMs = function measureElapsedMs(action, ...args) {\n"
    "  const started = prk.readClockMs();\n"
    "  action(...args);\n"
    "  return prk.readClockMs() - started;\n"
    "};\n"
    "prk.AssertionError = AssertionError;\n"
    "prk.Skip = Skip;\n"
    "})\n",
};

/** @brief Joins the prelude pieces into one malloc'd, NUL-terminated string. */
static char* prk_join_js_prelude(size_t* out_length) {
    size_t length = 0;
    for (size_t index = 0; index < sizeof prk_js_prelude / sizeof prk_js_prelude[0]; ++index) {
        length += strlen(prk_js_prelude[index]);
    }
    char* const text = (char*)malloc(length + 1);
    if (text == NULL) {
        return NULL;
    }
    size_t offset = 0;
    for (size_t index = 0; index < sizeof prk_js_prelude / sizeof prk_js_prelude[0]; ++index) {
        const size_t piece = strlen(prk_js_prelude[index]);
        memcpy(text + offset, prk_js_prelude[index], piece);
        offset += piece;
    }
    text[length] = '\0';
    *out_length = length;
    return text;
}

static const JSCFunctionListEntry prk_js_api_functions[] = {
    JS_CFUNC_DEF("addTest", 3, prk_js_add_test),
    JS_CFUNC_DEF("describe", 3, prk_js_describe),
    JS_CFUNC_MAGIC_DEF("beforeAll", 1, prk_js_add_suite_hook, PRK_HOOK_BEFORE_ALL),
    JS_CFUNC_MAGIC_DEF("afterAll", 1, prk_js_add_suite_hook, PRK_HOOK_AFTER_ALL),
    JS_CFUNC_MAGIC_DEF("beforeEach", 1, prk_js_add_suite_hook, PRK_HOOK_BEFORE_EACH),
    JS_CFUNC_MAGIC_DEF("afterEach", 1, prk_js_add_suite_hook, PRK_HOOK_AFTER_EACH),
    JS_CFUNC_DEF("readClockMs", 0, prk_js_read_clock_ms),
    JS_CFUNC_DEF("isFilePresent", 1, prk_js_is_file_present),
    JS_CFUNC_DEF("readTextFile", 1, prk_js_read_text_file),
};

/** @brief Runs the prelude with @p prk. @return 0, or -1 with an exception pending. */
static int prk_run_js_prelude(JSContext* context, JSValueConst prk) {
    size_t length = 0;
    char* const source = prk_join_js_prelude(&length);
    if (source == NULL) {
        JS_ThrowOutOfMemory(context);
        return -1;
    }
    JSValue install = JS_Eval(context, source, length, "prk", JS_EVAL_TYPE_GLOBAL);
    free(source);
    if (JS_IsException(install)) {
        return -1;
    }
    JSValue installed = JS_Call(context, install, JS_UNDEFINED, 1, &prk);
    JS_FreeValue(context, install);
    if (JS_IsException(installed)) {
        return -1;
    }
    JS_FreeValue(context, installed);
    return 0;
}

int prk_open_js_test_api(JSContext* context) {
    JSValue global = JS_GetGlobalObject(context);
    JSValue prk = JS_NewObject(context);
    JSValue console = JS_NewObject(context);
    int status = JS_IsException(prk) || JS_IsException(console) ? -1 : 0;
    if (status == 0) {
        status = JS_SetPropertyFunctionList(context, prk, prk_js_api_functions,
                                            (int)(sizeof prk_js_api_functions / sizeof prk_js_api_functions[0]));
    }
    if (status == 0) {
        JS_SetPropertyStr(context, prk, "version", JS_NewString(context, prk_get_version_string()));
        JS_SetPropertyStr(context, prk, "jsEngine", JS_NewString(context, prk_get_js_engine_version_string()));
        JS_SetPropertyStr(context, console, "log", JS_NewCFunction(context, prk_js_print, "log", 1));
        JS_SetPropertyStr(context, global, "print", JS_NewCFunction(context, prk_js_print, "print", 1));
        status = prk_add_js_process_api(context, prk);
    }
    if (status == 0) {
        status = prk_run_js_prelude(context, prk);
    }
    if (status == 0) {
        JS_SetPropertyStr(context, global, "console", JS_DupValue(context, console));
        JS_SetPropertyStr(context, global, "prk", JS_DupValue(context, prk));
    }
    JS_FreeValue(context, console);
    JS_FreeValue(context, prk);
    JS_FreeValue(context, global);
    return status;
}
