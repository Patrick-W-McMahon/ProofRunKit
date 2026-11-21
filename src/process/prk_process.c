/**
 * @file prk_process.c
 * @brief Portable part of the process-spawn layer (#13): request validation, defaults, result
 *        release, bounded output buffers, the environment merge, and Windows command-line quoting.
 *        No IO here; the operating-system adapters are prk_process_windows.c and prk_process_posix.c.
 */
#include <ctype.h>
#include <stdlib.h>
#include <string.h>

#include "prk_process_internal.h"

PrkProcessRunner prk_get_system_process_runner(void) {
    const PrkProcessRunner runner = {&prk_spawn_system_process, NULL};
    return runner;
}

/** @brief Reports whether every entry of a string array is non-NULL (and the array exists when needed). */
static int is_string_array_valid(const char* const* entries, size_t count) {
    if (count == 0) {
        return 1;
    }
    if (entries == NULL) {
        return 0;
    }
    for (size_t index = 0; index < count; ++index) {
        if (entries[index] == NULL) {
            return 0;
        }
    }
    return 1;
}

PrkStatus prk_check_process_request(const PrkProcessRequest* request) {
    if (request == NULL || request->executable == NULL || request->executable[0] == '\0' ||
        !is_string_array_valid(request->arguments, request->argument_count) ||
        !is_string_array_valid(request->environment, request->environment_count) ||
        (request->standard_input == NULL && request->standard_input_length != 0) ||
        request->timeout_ms > PRK_PROCESS_MAX_TIMEOUT_MS) {
        return PRK_STATUS_INVALID_ARGUMENT;
    }
    for (size_t index = 0; index < request->environment_count; ++index) {
        const char* const separator = strchr(request->environment[index], '=');
        if (separator == NULL || separator == request->environment[index]) {
            return PRK_STATUS_INVALID_ARGUMENT;
        }
    }
    return PRK_STATUS_OK;
}

uint64_t prk_get_effective_process_timeout_ms(const PrkProcessRequest* request) {
    return request->timeout_ms != 0 ? request->timeout_ms : PRK_PROCESS_DEFAULT_TIMEOUT_MS;
}

void prk_release_process_result(PrkProcessResult* result) {
    if (result == NULL) {
        return;
    }
    free(result->standard_output);
    free(result->standard_error);
    memset(result, 0, sizeof *result);
}

const char* prk_get_process_outcome_name(PrkProcessOutcome outcome) {
    switch (outcome) {
        case PRK_PROCESS_EXITED:
            return "exited";
        case PRK_PROCESS_TIMED_OUT:
            return "timed out";
        case PRK_PROCESS_FAILED_TO_START:
            return "failed to start";
        case PRK_PROCESS_SIGNALED:
            return "killed by a signal";
    }
    return "unknown";
}

/* ------------------------------------------------------------------ output buffers */

void prk_append_process_output(PrkOutputBuffer* buffer, const char* bytes, size_t length) {
    const size_t room = buffer->limit - buffer->length;
    const size_t kept = length < room ? length : room;
    if (kept < length) {
        buffer->is_truncated = 1;
    }
    if (kept == 0 || buffer->is_out_of_memory) {
        return;
    }
    if (buffer->length + kept > buffer->capacity) {
        size_t capacity = buffer->capacity == 0 ? 4096 : buffer->capacity;
        while (capacity < buffer->length + kept) {
            capacity = capacity > buffer->limit / 2 ? buffer->limit : capacity * 2;
        }
        char* const grown = (char*)realloc(buffer->bytes, capacity);
        if (grown == NULL) {
            buffer->is_out_of_memory = 1;
            return;
        }
        buffer->bytes = grown;
        buffer->capacity = capacity;
    }
    memcpy(buffer->bytes + buffer->length, bytes, kept);
    buffer->length += kept;
}

PrkStatus prk_take_process_output(PrkOutputBuffer* buffer, char** out_text, size_t* out_length, int* out_is_truncated) {
    char* const text = (char*)realloc(buffer->bytes, buffer->length + 1);
    if (text == NULL || buffer->is_out_of_memory) {
        free(text != NULL ? text : buffer->bytes);
        memset(buffer, 0, sizeof *buffer);
        return PRK_STATUS_OUT_OF_MEMORY;
    }
    text[buffer->length] = '\0';
    *out_text = text;
    *out_length = buffer->length;
    *out_is_truncated = buffer->is_truncated;
    memset(buffer, 0, sizeof *buffer);
    return PRK_STATUS_OK;
}

void prk_free_process_output(PrkOutputBuffer* buffer) {
    free(buffer->bytes);
    memset(buffer, 0, sizeof *buffer);
}

PrkStatus prk_set_process_start_failure(const char* message, PrkProcessResult* out_result) {
    memset(out_result, 0, sizeof *out_result);
    char* const empty_output = (char*)calloc(1, 1);
    char* const empty_error = (char*)calloc(1, 1);
    if (empty_output == NULL || empty_error == NULL) {
        free(empty_output);
        free(empty_error);
        return PRK_STATUS_OUT_OF_MEMORY;
    }
    out_result->outcome = PRK_PROCESS_FAILED_TO_START;
    out_result->standard_output = empty_output;
    out_result->standard_error = empty_error;
    const size_t length = strlen(message);
    const size_t copied = length < PRK_PROCESS_ERROR_CAPACITY - 1 ? length : PRK_PROCESS_ERROR_CAPACITY - 1;
    memcpy(out_result->error_message, message, copied);
    out_result->error_message[copied] = '\0';
    return PRK_STATUS_OK;
}

/* ------------------------------------------------------------------ environment */

/** @brief Reports whether two "NAME=..." entries have the same name. */
static int have_same_variable_name(const char* left, const char* right, int are_names_case_insensitive) {
    /* Windows keeps per-drive entries such as "=C:=C:\\dir": the name runs to the second '='. */
    size_t index = left[0] == '=' ? 1 : 0;
    if ((left[0] == '=') != (right[0] == '=')) {
        return 0;
    }
    for (;; ++index) {
        const char left_char = left[index] == '=' ? '\0' : left[index];
        const char right_char = right[index] == '=' ? '\0' : right[index];
        if (left_char == '\0' || right_char == '\0') {
            return left_char == right_char;
        }
        const int is_equal = are_names_case_insensitive
                                 ? tolower((unsigned char)left_char) == tolower((unsigned char)right_char)
                                 : left_char == right_char;
        if (!is_equal) {
            return 0;
        }
    }
}

/** @brief Reports whether @p entry's name appears in @p entries[first..count). */
static int is_variable_named_in(const char* entry, const char* const* entries, size_t first, size_t count,
                                int are_names_case_insensitive) {
    for (size_t index = first; index < count; ++index) {
        if (have_same_variable_name(entry, entries[index], are_names_case_insensitive)) {
            return 1;
        }
    }
    return 0;
}

PrkStatus prk_merge_process_environment(const char* const* parent_entries, size_t parent_count,
                                        const PrkProcessRequest* request, int are_names_case_insensitive,
                                        const char*** out_entries, size_t* out_count) {
    const size_t inherited_count = request->is_environment_inherited ? parent_count : 0;
    const char** const entries =
        (const char**)malloc((inherited_count + request->environment_count + 1) * sizeof *entries);
    if (entries == NULL) {
        return PRK_STATUS_OUT_OF_MEMORY;
    }
    size_t count = 0;
    for (size_t index = 0; index < inherited_count; ++index) {
        if (!is_variable_named_in(parent_entries[index], request->environment, 0, request->environment_count,
                                  are_names_case_insensitive)) {
            entries[count++] = parent_entries[index];
        }
    }
    for (size_t index = 0; index < request->environment_count; ++index) {
        if (!is_variable_named_in(request->environment[index], request->environment, index + 1,
                                  request->environment_count, are_names_case_insensitive)) {
            entries[count++] = request->environment[index];
        }
    }
    entries[count] = NULL;
    *out_entries = entries;
    *out_count = count;
    return PRK_STATUS_OK;
}

/* ------------------------------------------------------------------ Windows command line */

/** @brief Appends @p argument quoted per the CommandLineToArgvW rules; returns the new length. */
static size_t append_windows_argument(char* out_text, size_t length, const char* argument) {
    const int needs_quotes = argument[0] == '\0' || strpbrk(argument, " \t\n\v\"") != NULL;
    if (!needs_quotes) {
        const size_t size = strlen(argument);
        if (out_text != NULL) {
            memcpy(out_text + length, argument, size);
        }
        return length + size;
    }
    if (out_text != NULL) {
        out_text[length] = '"';
    }
    ++length;
    size_t backslashes = 0;
    for (const char* cursor = argument;; ++cursor) {
        if (*cursor == '\\') {
            ++backslashes;
            continue;
        }
        /* Backslashes are literal unless they precede a quote: double them there and before the closing quote. */
        const size_t copies = *cursor == '"' ? backslashes * 2 + 1 : (*cursor == '\0' ? backslashes * 2 : backslashes);
        for (size_t index = 0; index < copies; ++index) {
            if (out_text != NULL) {
                out_text[length] = '\\';
            }
            ++length;
        }
        backslashes = 0;
        if (*cursor == '\0') {
            break;
        }
        if (out_text != NULL) {
            out_text[length] = *cursor;
        }
        ++length;
    }
    if (out_text != NULL) {
        out_text[length] = '"';
    }
    return length + 1;
}

/** @brief Writes (or, with NULL @p out_text, measures) the whole command line. */
static size_t write_windows_command_line(char* out_text, const char* executable, const char* const* arguments,
                                         size_t argument_count) {
    /* The program name is parsed differently (no backslash escapes), so it is only ever wrapped in quotes. */
    const int quotes_program = executable[strcspn(executable, " \t")] != '\0';
    size_t length = 0;
    const size_t program_length = strlen(executable);
    if (out_text != NULL) {
        if (quotes_program) {
            out_text[0] = '"';
        }
        memcpy(out_text + (quotes_program ? 1 : 0), executable, program_length);
        if (quotes_program) {
            out_text[program_length + 1] = '"';
        }
    }
    length = program_length + (quotes_program ? 2 : 0);
    for (size_t index = 0; index < argument_count; ++index) {
        if (out_text != NULL) {
            out_text[length] = ' ';
        }
        length = append_windows_argument(out_text, length + 1, arguments[index]);
    }
    return length;
}

PrkStatus prk_format_windows_command_line(const char* executable, const char* const* arguments, size_t argument_count,
                                          char** out_command_line) {
    if (executable == NULL || strchr(executable, '"') != NULL || out_command_line == NULL) {
        return PRK_STATUS_INVALID_ARGUMENT;
    }
    const size_t length = write_windows_command_line(NULL, executable, arguments, argument_count);
    char* const text = (char*)malloc(length + 1);
    if (text == NULL) {
        return PRK_STATUS_OUT_OF_MEMORY;
    }
    (void)write_windows_command_line(text, executable, arguments, argument_count);
    text[length] = '\0';
    *out_command_line = text;
    return PRK_STATUS_OK;
}
