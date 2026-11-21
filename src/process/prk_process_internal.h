/**
 * @file prk_process_internal.h
 * @brief Pieces shared by the Windows and POSIX spawn adapters (#13): bounded output capture, the
 *        environment merge, and Windows command-line quoting. Pure helpers, testable on every host.
 *        Not public API.
 */
#ifndef PROOFRUNKIT_SRC_PROCESS_PRK_PROCESS_INTERNAL_H
#define PROOFRUNKIT_SRC_PROCESS_PRK_PROCESS_INTERNAL_H

#include <stddef.h>

#include "proofrunkit/prk_process.h"

#ifdef __cplusplus
extern "C" {
#endif

/** @brief A growing byte buffer that keeps at most @p limit bytes and records whether more arrived. */
typedef struct PrkOutputBuffer {
    char* bytes;          /**< malloc'd storage, or NULL while empty. */
    size_t length;        /**< Bytes kept. */
    size_t capacity;      /**< Bytes allocated. */
    size_t limit;         /**< Most bytes kept. */
    int is_truncated;     /**< 1 when bytes were dropped because of @p limit. */
    int is_out_of_memory; /**< 1 when growing failed (the kept bytes stay valid). */
} PrkOutputBuffer;

/**
 * @brief Appends bytes, keeping at most the buffer's limit.
 * @param buffer Buffer to grow (a documented mutator).
 * @param bytes Bytes to append. @param length Number of bytes.
 * @return Nothing. Allocation failure sets is_out_of_memory.
 */
void prk_append_process_output(PrkOutputBuffer* buffer, const char* bytes, size_t length);

/**
 * @brief Moves the buffer into a result field as a NUL-terminated malloc'd string.
 * @param buffer Buffer to empty (a documented mutator); its storage is transferred.
 * @param out_text Receives the text. @param out_length Receives its length. @param out_is_truncated Receives the flag.
 * @return PRK_STATUS_OK or PRK_STATUS_OUT_OF_MEMORY (then nothing is written and the buffer is freed).
 */
PrkStatus prk_take_process_output(PrkOutputBuffer* buffer, char** out_text, size_t* out_length, int* out_is_truncated);

/** @brief Frees a buffer's storage. @param buffer Buffer to free (a documented mutator). */
void prk_free_process_output(PrkOutputBuffer* buffer);

/**
 * @brief Merges the caller's environment with a request's entries. Entries are borrowed, not copied.
 * @param parent_entries "NAME=VALUE" strings of the caller (used only when the request inherits).
 * @param parent_count Entries in @p parent_entries.
 * @param request A valid request.
 * @param are_names_case_insensitive 1 on Windows, where PATH and Path are the same variable.
 * @param out_entries Receives a malloc'd array of borrowed pointers (free it with free()).
 * @param out_count Receives its length.
 * @return PRK_STATUS_OK or PRK_STATUS_OUT_OF_MEMORY. Request entries replace parent entries of the same name;
 *         among repeated request entries the last wins. Pure apart from the allocation.
 */
PrkStatus prk_merge_process_environment(const char* const* parent_entries, size_t parent_count,
                                        const PrkProcessRequest* request, int are_names_case_insensitive,
                                        const char*** out_entries, size_t* out_count);

/**
 * @brief Formats a Windows command line so CommandLineToArgvW and the MSVC C runtime parse it back into
 *        exactly @p executable followed by @p arguments.
 * @param executable Program name (quoted when it contains spaces; must not contain '"').
 * @param arguments Arguments. @param argument_count Number of arguments.
 * @param out_command_line Receives a malloc'd UTF-8 string.
 * @return PRK_STATUS_OK, PRK_STATUS_INVALID_ARGUMENT (a '"' in the executable), or PRK_STATUS_OUT_OF_MEMORY.
 *         Pure apart from the allocation.
 */
PrkStatus prk_format_windows_command_line(const char* executable, const char* const* arguments, size_t argument_count,
                                          char** out_command_line);

/**
 * @brief Clears @p out_result and sets it to "failed to start" with @p message.
 * @param message Reason, e.g. "executable not found: gcc". @param out_result Result to fill.
 * @return PRK_STATUS_OK, or PRK_STATUS_OUT_OF_MEMORY when the empty output strings cannot be allocated.
 */
PrkStatus prk_set_process_start_failure(const char* message, PrkProcessResult* out_result);

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* PROOFRUNKIT_SRC_PROCESS_PRK_PROCESS_INTERNAL_H */
