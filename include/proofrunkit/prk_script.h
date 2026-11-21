/**
 * @file prk_script.h
 * @brief Engine-neutral C ABI shared by the embedded script engines (Lua #9, JavaScript #11) for the
 *        test-authoring API (#12): resource limits, run settings, and the host services a script may use.
 *
 * Every capability a test script has beyond pure computation (printing, a clock, read-only file
 * checks) is supplied by the caller through PrkScriptHost, so the engines perform no ambient IO and
 * can be tested with fakes.
 */
#ifndef PROOFRUNKIT_PRK_SCRIPT_H
#define PROOFRUNKIT_PRK_SCRIPT_H

#include <stddef.h>
#include <stdint.h>

#include "proofrunkit/prk.h"
#include "proofrunkit/prk_msvc_toolchain.h"
#include "proofrunkit/prk_process.h"

#ifdef __cplusplus
extern "C" {
#endif

/** @brief Default memory limit for one script's interpreter state: 64 MiB. */
#define PRK_SCRIPT_DEFAULT_MEMORY_LIMIT_BYTES ((size_t)64 * 1024 * 1024)
/** @brief Default instruction budget for loading a script and for each test: 200 million. */
#define PRK_SCRIPT_DEFAULT_INSTRUCTION_LIMIT ((uint64_t)200000000)
/** @brief Recommended capacity for error-message buffers passed to the engines. */
#define PRK_SCRIPT_ERROR_CAPACITY 1024

/** @brief Resource limits for one script. A zero field selects the default. */
typedef struct PrkScriptLimits {
    size_t memory_limit_bytes;  /**< Maximum bytes the interpreter state may hold at once. */
    uint64_t instruction_limit; /**< Maximum VM instructions for the load and for each test. */
} PrkScriptLimits;

/** @brief Result of a host file request. */
typedef enum PrkFileAccessStatus {
    PRK_FILE_ACCESS_OK = 0,        /**< The request succeeded. */
    PRK_FILE_ACCESS_NOT_FOUND = 1, /**< No such file. */
    PRK_FILE_ACCESS_DENIED = 2,    /**< The path is outside what scripts may read (absolute, "..", ...). */
    PRK_FILE_ACCESS_READ_ERROR = 3 /**< The file exists but could not be read. */
} PrkFileAccessStatus;

/** @brief Receives file bytes from PrkScriptHost.read_file; may be called several times. */
typedef void (*PrkReceiveFileBytes)(void* sink, const char* bytes, size_t length);

/**
 * @brief Services the caller grants to test scripts. Any NULL callback disables that capability, and
 *        the script gets a clear error if it uses it.
 *
 * Callbacks must not throw or longjmp; they are called from engine C code.
 */
typedef struct PrkScriptHost {
    /** Receives @p length bytes of print() output (not NUL-terminated). */
    void (*write_text)(void* host_state, const char* text, size_t length);
    /** Returns a monotonic time in milliseconds; used for per-test timeouts and timing. */
    uint64_t (*read_monotonic_milliseconds)(void* host_state);
    /** Reports whether a readable regular file exists at @p path (OK or NOT_FOUND), or DENIED. */
    PrkFileAccessStatus (*find_file)(void* host_state, const char* path);
    /** Streams the whole file at @p path to @p receive_bytes. */
    PrkFileAccessStatus (*read_file)(void* host_state, const char* path, PrkReceiveFileBytes receive_bytes, void* sink);
    void* host_state; /**< Passed back to every callback unchanged; may be NULL. Not owned. */
    /** Spawns processes for prk.run_process / prk.runProcess (#12, #13); a NULL spawn_process disables it. */
    PrkProcessRunner process_runner;
    /** MSVC discovery cache for prk.compile (#14), shared by every script of a run; NULL disables caching. */
    PrkMsvcToolchain* msvc_toolchain;
} PrkScriptHost;

/**
 * @brief Which tests a script registers. Tests that are not selected are never registered, so they
 *        are neither listed nor run, and their suites' hooks do not run for them.
 */
typedef struct PrkScriptRunSettings {
    const char* name_filter;         /**< Substring of the full test name; NULL or "" selects all. */
    const char* const* include_tags; /**< When non-empty, a test needs at least one of these tags. */
    size_t include_tag_count;        /**< Entries in @p include_tags. */
    const char* const* exclude_tags; /**< A test with any of these tags is not selected. */
    size_t exclude_tag_count;        /**< Entries in @p exclude_tags. */
    uint64_t default_timeout_ms;     /**< Per-test wall-clock timeout when a test sets none; 0 = none. */
} PrkScriptRunSettings;

/**
 * @brief Returns the human-readable name of a file access status, e.g. "not found".
 * @param status Status to name.
 * @return A static string. Pure function.
 */
const char* prk_get_file_access_status_name(PrkFileAccessStatus status);

/**
 * @brief Chooses the timeout of a process a script starts, so it can never outlive the running test.
 * @param requested_ms Timeout the script asked for; 0 means the process layer's default.
 * @param deadline_ms Host-clock time the running test must finish by; 0 when there is none.
 * @param now_ms Current host-clock time.
 * @return The smaller of the effective request and the time left before the deadline (at least 1 ms).
 *         Pure function.
 */
uint64_t prk_cap_script_process_timeout_ms(uint64_t requested_ms, uint64_t deadline_ms, uint64_t now_ms);

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* PROOFRUNKIT_PRK_SCRIPT_H */
