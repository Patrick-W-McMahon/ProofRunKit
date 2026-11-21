/**
 * @file prk_process_windows.c
 * @brief Windows adapter of the process-spawn layer (#13): CreateProcessW with only the three pipe
 *        handles inherited, the child placed in a kill-on-close job object (so a timeout kills the whole
 *        process tree), one thread per pipe so stdout and stderr cannot deadlock, and a monotonic deadline.
 *
 * This is an IO edge: everything decidable without the operating system lives in prk_process.c. It never
 * uses cmd.exe; arguments are quoted for CommandLineToArgvW by prk_format_windows_command_line().
 *
 * Size: just over the 400-line review mark; the UTF-8/UTF-16 and environment-block conversions are kept
 * beside the CreateProcessW call that needs them rather than split into a one-caller file.
 */
#if defined(_WIN32)

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "prk_process_internal.h"

/** @brief One pipe drained by a reader thread. */
typedef struct PipeReader {
    HANDLE pipe;
    PrkOutputBuffer buffer;
} PipeReader;

/** @brief The child's stdin, fed by a writer thread. */
typedef struct PipeWriter {
    HANDLE pipe;
    const char* bytes;
    size_t length;
} PipeWriter;

/** @brief Handles of one spawn, released together. */
typedef struct WindowsSpawn {
    HANDLE child_input;  /**< Child's stdin read end (inherited). */
    HANDLE child_output; /**< Child's stdout write end (inherited). */
    HANDLE child_error;  /**< Child's stderr write end (inherited). */
    PipeWriter writer;
    PipeReader output;
    PipeReader error;
    HANDLE job;
    HANDLE threads[3]; /**< stdout reader, stderr reader, stdin writer. */
} WindowsSpawn;

static void close_handle(HANDLE* handle) {
    if (*handle != NULL && *handle != INVALID_HANDLE_VALUE) {
        (void)CloseHandle(*handle);
    }
    *handle = NULL;
}

static DWORD WINAPI read_pipe_until_closed(LPVOID parameter) {
    PipeReader* const reader = (PipeReader*)parameter;
    char chunk[8192];
    DWORD count = 0;
    while (ReadFile(reader->pipe, chunk, (DWORD)sizeof chunk, &count, NULL) && count > 0) {
        prk_append_process_output(&reader->buffer, chunk, count);
    }
    return 0;
}

static DWORD WINAPI write_pipe_then_close(LPVOID parameter) {
    PipeWriter* const writer = (PipeWriter*)parameter;
    size_t written = 0;
    while (written < writer->length) {
        const size_t remaining = writer->length - written;
        DWORD count = 0;
        if (!WriteFile(writer->pipe, writer->bytes + written, remaining > 65536 ? 65536u : (DWORD)remaining, &count,
                       NULL)) {
            break; /* The child exited or closed its stdin. */
        }
        written += count;
    }
    close_handle(&writer->pipe);
    return 0;
}

/** @brief Converts UTF-8 to a malloc'd UTF-16 string, or NULL. */
static wchar_t* convert_to_wide(const char* text) {
    const int length = MultiByteToWideChar(CP_UTF8, 0, text, -1, NULL, 0);
    wchar_t* const wide = length > 0 ? (wchar_t*)malloc((size_t)length * sizeof *wide) : NULL;
    if (wide != NULL && MultiByteToWideChar(CP_UTF8, 0, text, -1, wide, length) != length) {
        free(wide);
        return NULL;
    }
    return wide;
}

/** @brief Converts UTF-16 to a malloc'd UTF-8 string, or NULL. */
static char* convert_to_utf8(const wchar_t* wide) {
    const int length = WideCharToMultiByte(CP_UTF8, 0, wide, -1, NULL, 0, NULL, NULL);
    char* const text = length > 0 ? (char*)malloc((size_t)length) : NULL;
    if (text != NULL && WideCharToMultiByte(CP_UTF8, 0, wide, -1, text, length, NULL, NULL) != length) {
        free(text);
        return NULL;
    }
    return text;
}

/** @brief Frees an array of malloc'd strings. */
static void free_strings(char** strings, size_t count) {
    for (size_t index = 0; strings != NULL && index < count; ++index) {
        free(strings[index]);
    }
    free(strings);
}

/** @brief Reads the caller's environment as UTF-8 strings. @return 1 on success. */
static int read_parent_environment(char*** out_entries, size_t* out_count) {
    wchar_t* const block = GetEnvironmentStringsW();
    if (block == NULL) {
        return 0;
    }
    size_t count = 0;
    for (const wchar_t* cursor = block; *cursor != L'\0'; cursor += wcslen(cursor) + 1) {
        ++count;
    }
    char** const entries = (char**)calloc(count + 1, sizeof *entries);
    size_t index = 0;
    int is_complete = entries != NULL;
    for (const wchar_t* cursor = block; is_complete && *cursor != L'\0'; cursor += wcslen(cursor) + 1) {
        entries[index] = convert_to_utf8(cursor);
        is_complete = entries[index++] != NULL;
    }
    (void)FreeEnvironmentStringsW(block);
    if (!is_complete) {
        free_strings(entries, index);
        return 0;
    }
    *out_entries = entries;
    *out_count = count;
    return 1;
}

/** @brief Joins entries into a CREATE_UNICODE_ENVIRONMENT block. @return malloc'd block, or NULL. */
static wchar_t* build_environment_block(const char* const* entries, size_t count) {
    size_t length = 1;
    wchar_t** const wide_entries = (wchar_t**)calloc(count + 1, sizeof *wide_entries);
    int is_complete = wide_entries != NULL;
    for (size_t index = 0; is_complete && index < count; ++index) {
        wide_entries[index] = convert_to_wide(entries[index]);
        is_complete = wide_entries[index] != NULL;
        length += is_complete ? wcslen(wide_entries[index]) + 1 : 0;
    }
    wchar_t* block = is_complete ? (wchar_t*)malloc((length + 1) * sizeof *block) : NULL;
    if (block != NULL) {
        size_t offset = 0;
        for (size_t index = 0; index < count; ++index) {
            const size_t size = wcslen(wide_entries[index]) + 1;
            memcpy(block + offset, wide_entries[index], size * sizeof *block);
            offset += size;
        }
        block[offset] = L'\0';
        block[offset + 1] = L'\0'; /* An empty environment still needs two terminators. */
    }
    for (size_t index = 0; wide_entries != NULL && index < count; ++index) {
        free(wide_entries[index]);
    }
    free(wide_entries);
    return block;
}

/** @brief Builds the environment block for a request; leaves *out_block NULL to inherit the caller's unchanged. */
static PrkStatus prepare_environment(const PrkProcessRequest* request, wchar_t** out_block) {
    *out_block = NULL;
    if (request->is_environment_inherited && request->environment_count == 0) {
        return PRK_STATUS_OK;
    }
    char** parent = NULL;
    size_t parent_count = 0;
    if (request->is_environment_inherited && !read_parent_environment(&parent, &parent_count)) {
        return PRK_STATUS_OUT_OF_MEMORY;
    }
    const char** merged = NULL;
    size_t merged_count = 0;
    PrkStatus status =
        prk_merge_process_environment((const char* const*)parent, parent_count, request, 1, &merged, &merged_count);
    if (status == PRK_STATUS_OK) {
        *out_block = build_environment_block(merged, merged_count);
        status = *out_block != NULL ? PRK_STATUS_OK : PRK_STATUS_OUT_OF_MEMORY;
    }
    free((void*)merged);
    free_strings(parent, parent_count);
    return status;
}

/** @brief Creates an inheritable pipe; the parent's end is made non-inheritable. @return 1 on success. */
static int open_pipe(HANDLE* out_child_end, HANDLE* out_parent_end, int is_child_reading) {
    SECURITY_ATTRIBUTES attributes = {sizeof attributes, NULL, TRUE};
    HANDLE read_end = NULL;
    HANDLE write_end = NULL;
    if (!CreatePipe(&read_end, &write_end, &attributes, 0)) {
        return 0;
    }
    *out_child_end = is_child_reading ? read_end : write_end;
    *out_parent_end = is_child_reading ? write_end : read_end;
    return SetHandleInformation(*out_parent_end, HANDLE_FLAG_INHERIT, 0) != 0;
}

/** @brief Creates the job object that kills the child and its descendants when closed or terminated. */
static HANDLE create_kill_on_close_job(void) {
    HANDLE job = CreateJobObjectW(NULL, NULL);
    JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits;
    memset(&limits, 0, sizeof limits);
    limits.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
    if (job != NULL && !SetInformationJobObject(job, JobObjectExtendedLimitInformation, &limits, sizeof limits)) {
        close_handle(&job);
    }
    return job;
}

/** @brief Describes a CreateProcessW failure. */
static void describe_start_error(DWORD error, const PrkProcessRequest* request, char* out_text, size_t capacity) {
    if (error == ERROR_FILE_NOT_FOUND || error == ERROR_PATH_NOT_FOUND) {
        (void)snprintf(out_text, capacity, "executable not found: %s", request->executable);
    } else if (error == ERROR_DIRECTORY) {
        (void)snprintf(out_text, capacity, "cannot change to working directory '%s': not a directory",
                       request->working_directory != NULL ? request->working_directory : "");
    } else if (error == ERROR_ACCESS_DENIED) {
        (void)snprintf(out_text, capacity, "cannot start '%s': access denied", request->executable);
    } else if (error == ERROR_BAD_EXE_FORMAT) {
        (void)snprintf(out_text, capacity, "cannot start '%s': not a valid Windows program", request->executable);
    } else {
        (void)snprintf(out_text, capacity, "cannot start '%s': Windows error %lu", request->executable,
                       (unsigned long)error);
    }
}

/** @brief Calls CreateProcessW inheriting only the three pipe handles. @return 0 on success or the error. */
static DWORD create_child_process(WindowsSpawn* spawn, wchar_t* command_line, const wchar_t* directory,
                                  wchar_t* environment, PROCESS_INFORMATION* out_process) {
    HANDLE inherited[3] = {spawn->child_input, spawn->child_output, spawn->child_error};
    SIZE_T attribute_size = 0;
    (void)InitializeProcThreadAttributeList(NULL, 1, 0, &attribute_size);
    LPPROC_THREAD_ATTRIBUTE_LIST attributes = (LPPROC_THREAD_ATTRIBUTE_LIST)malloc(attribute_size);
    if (attributes == NULL || !InitializeProcThreadAttributeList(attributes, 1, 0, &attribute_size)) {
        free(attributes);
        return ERROR_NOT_ENOUGH_MEMORY;
    }
    DWORD error = 0;
    if (!UpdateProcThreadAttribute(attributes, 0, PROC_THREAD_ATTRIBUTE_HANDLE_LIST, inherited, sizeof inherited, NULL,
                                   NULL)) {
        error = GetLastError();
    } else {
        STARTUPINFOEXW startup;
        memset(&startup, 0, sizeof startup);
        startup.StartupInfo.cb = sizeof startup;
        startup.StartupInfo.dwFlags = STARTF_USESTDHANDLES;
        startup.StartupInfo.hStdInput = spawn->child_input;
        startup.StartupInfo.hStdOutput = spawn->child_output;
        startup.StartupInfo.hStdError = spawn->child_error;
        startup.lpAttributeList = attributes;
        const DWORD flags =
            CREATE_SUSPENDED | CREATE_UNICODE_ENVIRONMENT | CREATE_NO_WINDOW | EXTENDED_STARTUPINFO_PRESENT;
        if (!CreateProcessW(NULL, command_line, NULL, NULL, TRUE, flags, environment, directory, &startup.StartupInfo,
                            out_process)) {
            error = GetLastError();
        }
    }
    DeleteProcThreadAttributeList(attributes);
    free(attributes);
    return error;
}

/** @brief Waits for the process, then for the pipe threads; kills the job at the deadline. */
static int wait_for_child(WindowsSpawn* spawn, HANDLE process, ULONGLONG deadline_ms) {
    int is_killed = 0;
    ULONGLONG now = GetTickCount64();
    if (WaitForSingleObject(process, now < deadline_ms ? (DWORD)(deadline_ms - now) : 0) != WAIT_OBJECT_0) {
        if (spawn->job == NULL || !TerminateJobObject(spawn->job, 1)) {
            (void)TerminateProcess(process, 1);
        }
        (void)WaitForSingleObject(process, INFINITE);
        is_killed = 1;
    }
    /* Descendants may still hold the pipes open after the child exits: give them until the deadline. */
    now = GetTickCount64();
    if (WaitForMultipleObjects(3, spawn->threads, TRUE, now < deadline_ms ? (DWORD)(deadline_ms - now) : 0) !=
        WAIT_OBJECT_0) {
        if (spawn->job != NULL) {
            (void)TerminateJobObject(spawn->job, 1);
        }
        if (WaitForMultipleObjects(3, spawn->threads, TRUE, 2000) != WAIT_OBJECT_0) {
            for (int index = 0; index < 3; ++index) {
                (void)CancelSynchronousIo(spawn->threads[index]);
            }
            (void)WaitForMultipleObjects(3, spawn->threads, TRUE, INFINITE);
        }
    }
    return is_killed;
}

/** @brief Starts the pipe threads. @return 1 on success. */
static int start_pipe_threads(WindowsSpawn* spawn) {
    spawn->threads[0] = CreateThread(NULL, 64 * 1024, &read_pipe_until_closed, &spawn->output, 0, NULL);
    spawn->threads[1] = CreateThread(NULL, 64 * 1024, &read_pipe_until_closed, &spawn->error, 0, NULL);
    spawn->threads[2] = CreateThread(NULL, 64 * 1024, &write_pipe_then_close, &spawn->writer, 0, NULL);
    return spawn->threads[0] != NULL && spawn->threads[1] != NULL && spawn->threads[2] != NULL;
}

/** @brief Fills the result from a finished spawn. */
static PrkStatus finish_result(WindowsSpawn* spawn, HANDLE process, int is_killed, ULONGLONG started_ms,
                               PrkProcessResult* out_result) {
    memset(out_result, 0, sizeof *out_result);
    DWORD exit_code = 0;
    (void)GetExitCodeProcess(process, &exit_code);
    out_result->outcome = is_killed ? PRK_PROCESS_TIMED_OUT : PRK_PROCESS_EXITED;
    out_result->exit_code = is_killed ? 0 : (int)exit_code;
    out_result->elapsed_ms = (uint64_t)(GetTickCount64() - started_ms);
    if (prk_take_process_output(&spawn->output.buffer, &out_result->standard_output,
                                &out_result->standard_output_length,
                                &out_result->is_standard_output_truncated) != PRK_STATUS_OK) {
        return PRK_STATUS_OUT_OF_MEMORY;
    }
    if (prk_take_process_output(&spawn->error.buffer, &out_result->standard_error, &out_result->standard_error_length,
                                &out_result->is_standard_error_truncated) != PRK_STATUS_OK) {
        prk_release_process_result(out_result);
        return PRK_STATUS_OUT_OF_MEMORY;
    }
    return PRK_STATUS_OK;
}

/** @brief Starts the child with prepared strings and pipes, waits, and fills the result. */
static PrkStatus spawn_prepared(WindowsSpawn* spawn, const PrkProcessRequest* request, wchar_t* command_line,
                                const wchar_t* directory, wchar_t* environment, PrkProcessResult* out_result) {
    PROCESS_INFORMATION process;
    memset(&process, 0, sizeof process);
    const ULONGLONG started_ms = GetTickCount64();
    const ULONGLONG deadline_ms = started_ms + prk_get_effective_process_timeout_ms(request);
    const DWORD error = create_child_process(spawn, command_line, directory, environment, &process);
    close_handle(&spawn->child_input);
    close_handle(&spawn->child_output);
    close_handle(&spawn->child_error);
    if (error != 0) {
        char message[PRK_PROCESS_ERROR_CAPACITY];
        describe_start_error(error, request, message, sizeof message);
        return prk_set_process_start_failure(message, out_result);
    }
    spawn->job = create_kill_on_close_job();
    if (spawn->job != NULL && !AssignProcessToJobObject(spawn->job, process.hProcess)) {
        close_handle(&spawn->job); /* Fall back to killing only the child on timeout. */
    }
    PrkStatus status = PRK_STATUS_OUT_OF_MEMORY;
    if (start_pipe_threads(spawn)) {
        (void)ResumeThread(process.hThread);
        const int is_killed = wait_for_child(spawn, process.hProcess, deadline_ms);
        status = finish_result(spawn, process.hProcess, is_killed, started_ms, out_result);
    } else {
        (void)TerminateProcess(process.hProcess, 1);
        (void)ResumeThread(process.hThread);
    }
    close_handle(&process.hThread);
    close_handle(&process.hProcess);
    return status;
}

/** @brief Releases every handle and buffer of a spawn. */
static void release_spawn(WindowsSpawn* spawn) {
    close_handle(&spawn->child_input);
    close_handle(&spawn->child_output);
    close_handle(&spawn->child_error);
    for (int index = 0; index < 3; ++index) {
        if (spawn->threads[index] != NULL) {
            (void)WaitForSingleObject(spawn->threads[index], INFINITE);
            close_handle(&spawn->threads[index]);
        }
    }
    close_handle(&spawn->writer.pipe);
    close_handle(&spawn->output.pipe);
    close_handle(&spawn->error.pipe);
    close_handle(&spawn->job);
    prk_free_process_output(&spawn->output.buffer);
    prk_free_process_output(&spawn->error.buffer);
}

PrkStatus prk_spawn_system_process(void* runner_state, const PrkProcessRequest* request, PrkProcessResult* out_result) {
    (void)runner_state;
    if (out_result == NULL || prk_check_process_request(request) != PRK_STATUS_OK) {
        return PRK_STATUS_INVALID_ARGUMENT;
    }
    char* command_line_utf8 = NULL;
    PrkStatus status = prk_format_windows_command_line(request->executable, request->arguments, request->argument_count,
                                                       &command_line_utf8);
    wchar_t* const command_line = status == PRK_STATUS_OK ? convert_to_wide(command_line_utf8) : NULL;
    const int has_directory = request->working_directory != NULL && request->working_directory[0] != '\0';
    wchar_t* const directory = has_directory ? convert_to_wide(request->working_directory) : NULL;
    wchar_t* environment = NULL;
    if (status == PRK_STATUS_OK) {
        status = command_line == NULL || (has_directory && directory == NULL)
                     ? PRK_STATUS_OUT_OF_MEMORY
                     : prepare_environment(request, &environment);
    }
    WindowsSpawn spawn;
    memset(&spawn, 0, sizeof spawn);
    const size_t limit =
        request->output_limit_bytes != 0 ? request->output_limit_bytes : PRK_PROCESS_DEFAULT_OUTPUT_LIMIT_BYTES;
    spawn.output.buffer.limit = spawn.error.buffer.limit = limit;
    spawn.writer.bytes = request->standard_input;
    spawn.writer.length = request->standard_input_length;
    if (status == PRK_STATUS_OK) {
        if (!open_pipe(&spawn.child_input, &spawn.writer.pipe, 1) ||
            !open_pipe(&spawn.child_output, &spawn.output.pipe, 0) ||
            !open_pipe(&spawn.child_error, &spawn.error.pipe, 0)) {
            status = prk_set_process_start_failure("cannot start a process: no pipes available", out_result);
        } else {
            status = spawn_prepared(&spawn, request, command_line, directory, environment, out_result);
        }
    }
    release_spawn(&spawn);
    free(environment);
    free(directory);
    free(command_line);
    free(command_line_utf8);
    return status;
}

#endif /* _WIN32 */
