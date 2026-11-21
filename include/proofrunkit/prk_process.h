/**
 * @file prk_process.h
 * @brief Process-spawn layer (#13): start a program with arguments, environment, working directory,
 *        optional standard input, and a mandatory timeout; capture stdout, stderr, the exit code, and
 *        the elapsed time.
 *
 * Compiler drivers, the assembler pass, the Python and Node.js runners, and the script API all spawn
 * processes through a PrkProcessRunner, so tests can inject a fake runner instead of real processes.
 * prk_get_system_process_runner() returns the real one (Windows: CreateProcessW in a kill-on-close job
 * object; POSIX: fork/exec in its own process group). This module does not interpret output and
 * never runs a command through a shell.
 */
#ifndef PROOFRUNKIT_PRK_PROCESS_H
#define PROOFRUNKIT_PRK_PROCESS_H

#include <stddef.h>
#include <stdint.h>

#include "proofrunkit/prk.h"

#ifdef __cplusplus
extern "C" {
#endif

/** @brief Timeout used when a request sets timeout_ms to 0: no spawn waits forever. */
#define PRK_PROCESS_DEFAULT_TIMEOUT_MS ((uint64_t)60000)
/** @brief Largest timeout a request may set (24 hours). */
#define PRK_PROCESS_MAX_TIMEOUT_MS ((uint64_t)86400000)
/** @brief Bytes kept per output stream when a request sets output_limit_bytes to 0 (16 MiB). */
#define PRK_PROCESS_DEFAULT_OUTPUT_LIMIT_BYTES ((size_t)16 * 1024 * 1024)
/** @brief Capacity of PrkProcessResult.error_message, including the terminator. */
#define PRK_PROCESS_ERROR_CAPACITY 256

/**
 * @brief What to start. Every pointer is borrowed for the duration of the call and never modified.
 *
 * Strings are UTF-8. The executable is started directly, never through a shell, so arguments need no
 * shell quoting. A bare name (no path separator) is searched for on the parent's PATH.
 */
typedef struct PrkProcessRequest {
    const char* executable;         /**< Program path or bare name. Required, non-empty. */
    const char* const* arguments;   /**< Arguments after the program name; may be NULL when count is 0. */
    size_t argument_count;          /**< Entries in @p arguments. */
    const char* working_directory;  /**< Directory to start in; NULL or "" inherits the caller's. */
    const char* const* environment; /**< "NAME=VALUE" entries; NULL when count is 0. */
    size_t environment_count;       /**< Entries in @p environment. */
    int is_environment_inherited;   /**< 1: start from the caller's environment and apply @p environment
                                         on top (same name replaces); 0: use only @p environment. */
    const char* standard_input;     /**< Bytes written to the child's stdin, then closed; may be NULL. */
    size_t standard_input_length;   /**< Bytes in @p standard_input. */
    uint64_t timeout_ms;            /**< Wall-clock limit; 0 selects PRK_PROCESS_DEFAULT_TIMEOUT_MS. */
    size_t output_limit_bytes;      /**< Bytes kept per stream; 0 selects the default. More is drained and dropped. */
} PrkProcessRequest;

/** @brief How a spawned process ended. */
typedef enum PrkProcessOutcome {
    PRK_PROCESS_EXITED = 0,          /**< The process exited by itself; exit_code is valid. */
    PRK_PROCESS_TIMED_OUT = 1,       /**< The timeout passed; the process and its children were killed. */
    PRK_PROCESS_FAILED_TO_START = 2, /**< The program was missing or could not be started; see error_message. */
    PRK_PROCESS_SIGNALED = 3         /**< POSIX only: the process was ended by a signal (termination_signal). */
} PrkProcessOutcome;

/**
 * @brief What happened. Output buffers are allocated with malloc (always NUL-terminated, may contain NUL
 *        bytes) and released by prk_release_process_result().
 */
typedef struct PrkProcessResult {
    PrkProcessOutcome outcome;                      /**< How the process ended. */
    int exit_code;                                  /**< Exit code when outcome is EXITED (Windows codes are 32-bit). */
    int termination_signal;                         /**< Signal number when outcome is SIGNALED, else 0. */
    char* standard_output;                          /**< Captured stdout; never NULL after a successful call. */
    size_t standard_output_length;                  /**< Bytes in @p standard_output. */
    int is_standard_output_truncated;               /**< 1 when stdout exceeded the output limit. */
    char* standard_error;                           /**< Captured stderr; never NULL after a successful call. */
    size_t standard_error_length;                   /**< Bytes in @p standard_error. */
    int is_standard_error_truncated;                /**< 1 when stderr exceeded the output limit. */
    uint64_t elapsed_ms;                            /**< Wall-clock time from start to exit or kill. */
    char error_message[PRK_PROCESS_ERROR_CAPACITY]; /**< Why the start failed (FAILED_TO_START), else "". */
} PrkProcessResult;

/**
 * @brief Spawns one process and waits for it.
 * @param runner_state The runner's own state (PrkProcessRunner.runner_state).
 * @param request What to start; not modified.
 * @param out_result Receives the result on PRK_STATUS_OK; release it with prk_release_process_result().
 * @return PRK_STATUS_OK when the request was valid and the result is filled in (including a program that
 *         failed to start or timed out), PRK_STATUS_INVALID_ARGUMENT, or PRK_STATUS_OUT_OF_MEMORY.
 */
typedef PrkStatus (*PrkSpawnProcess)(void* runner_state, const PrkProcessRequest* request,
                                     PrkProcessResult* out_result);

/** @brief An injectable process runner: the real one, or a fake in tests. */
typedef struct PrkProcessRunner {
    PrkSpawnProcess spawn_process; /**< Spawns and waits; see PrkSpawnProcess. */
    void* runner_state;            /**< Passed back to spawn_process unchanged; may be NULL. Not owned. */
} PrkProcessRunner;

/**
 * @brief Returns the runner that starts real processes on this operating system.
 * @return A runner with no state. Pure function.
 */
PrkProcessRunner prk_get_system_process_runner(void);

/**
 * @brief Spawns a real process (the system runner's spawn_process).
 * @param runner_state Ignored; may be NULL.
 * @param request What to start; validated with prk_check_process_request().
 * @param out_result Receives the result on PRK_STATUS_OK.
 * @return See PrkSpawnProcess. Side effects: starts a process, reads its pipes, may kill it on timeout.
 */
PrkStatus prk_spawn_system_process(void* runner_state, const PrkProcessRequest* request, PrkProcessResult* out_result);

/**
 * @brief Copies the value of this process's environment variable @p name into @p out_value
 *        (_dupenv_s on MSVC, getenv elsewhere).
 * @param name Variable name. @param out_value Receives the value, or "" when it is unset or does not fit.
 * @param capacity Bytes in @p out_value.
 * @return 1 when the variable is set and fits, else 0. Side effects: none (reads the environment).
 */
int prk_read_environment_variable(const char* name, char* out_value, size_t capacity);

/**
 * @brief Validates a request: executable present and non-empty, arrays non-NULL when their counts are
 *        non-zero, no NULL entries, "NAME=VALUE" environment entries with a non-empty name, and a timeout
 *        no larger than PRK_PROCESS_MAX_TIMEOUT_MS.
 * @param request Request to check; may be NULL.
 * @return PRK_STATUS_OK or PRK_STATUS_INVALID_ARGUMENT. Pure function.
 */
PrkStatus prk_check_process_request(const PrkProcessRequest* request);

/**
 * @brief Returns the timeout a request will use: its timeout_ms, or the default when it is 0.
 * @param request A valid request.
 * @return Milliseconds. Pure function.
 */
uint64_t prk_get_effective_process_timeout_ms(const PrkProcessRequest* request);

/**
 * @brief Frees the output buffers of a result and resets it to an empty EXITED result.
 * @param result Result filled by a spawn; may be NULL. Safe to call twice.
 * @return Nothing. Side effects: frees memory owned by @p result (a documented mutator).
 */
void prk_release_process_result(PrkProcessResult* result);

/**
 * @brief Returns a short name for an outcome, e.g. "timed out".
 * @param outcome Outcome to name.
 * @return A static string. Pure function.
 */
const char* prk_get_process_outcome_name(PrkProcessOutcome outcome);

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* PROOFRUNKIT_PRK_PROCESS_H */
