# Process-spawn layer

Compiler drivers (#14, #15, #16), the Python and Node.js runners (#17, #18), and the script API (#12) start
programs through one small C layer ([#13](https://github.com/Patrick-W-McMahon/ProofRunKit/issues/13)), declared in
[`include/proofrunkit/prk_process.h`](../include/proofrunkit/prk_process.h).

```c
const char* const arguments[] = {"-c", "hello.c", "-o", "hello.o"};
const PrkProcessRequest request = {
    .executable = "gcc", .arguments = arguments, .argument_count = 4,
    .is_environment_inherited = 1, .timeout_ms = 30000,
};
PrkProcessResult result;
const PrkProcessRunner runner = prk_get_system_process_runner();
if (runner.spawn_process(runner.runner_state, &request, &result) == PRK_STATUS_OK) {
    /* result.outcome, result.exit_code, result.standard_output / standard_error, result.elapsed_ms */
    prk_release_process_result(&result);
}
```

## Contract

| Topic | Behavior |
| --- | --- |
| Program | `executable` is a path or a bare name. Bare names are found on the caller's `PATH` (Windows: the `CreateProcessW` search order, which adds `.exe`). No shell is ever used, so arguments are passed exactly, spaces and quotes included. |
| Arguments | On Windows they are quoted for `CommandLineToArgvW` / the MSVC runtime by `prk_format_windows_command_line()`. A program path may not contain `"`. |
| Environment | `is_environment_inherited = 1` starts from the caller's environment and applies `environment` (`"NAME=VALUE"`) on top; the same name replaces (case-insensitively on Windows). `0` passes only `environment`. |
| Working directory | `working_directory`, or the caller's when NULL or empty. A missing directory is a start failure. |
| Standard input | `standard_input` bytes are written, then stdin is closed; with none the child sees end of file at once. |
| Timeout | Always on: `timeout_ms`, or 60 s when 0, at most 24 h. At the deadline the process **and its descendants** are killed (Windows: a kill-on-close job object; POSIX: SIGKILL to its process group) and the outcome is `PRK_PROCESS_TIMED_OUT`. |
| Output | stdout and stderr are captured separately and concurrently (no pipe deadlock). Each keeps at most `output_limit_bytes` (16 MiB by default); the rest is drained, dropped, and flagged `is_standard_*_truncated`. Buffers are NUL-terminated and may contain NUL bytes. |
| Outcome | `PRK_PROCESS_EXITED` (with `exit_code`; Windows codes are 32-bit, e.g. `0xC0000005` reads as a negative `int`), `PRK_PROCESS_TIMED_OUT`, `PRK_PROCESS_FAILED_TO_START` (with `error_message`, e.g. `executable not found: gcc`), or `PRK_PROCESS_SIGNALED` (POSIX). |
| Errors | `PRK_STATUS_INVALID_ARGUMENT` for a malformed request, `PRK_STATUS_OUT_OF_MEMORY`; a missing program or a timeout is **not** an error status but a result, so callers always report it. |
| Ownership | The request is borrowed and never modified. The result is written only on `PRK_STATUS_OK` and owns its buffers until `prk_release_process_result()`. |

## Injection

Code that spawns takes a `PrkProcessRunner` (`spawn_process` plus `runner_state`) instead of calling the system
directly, so tests can use a fake runner that returns canned results. `prk_get_system_process_runner()` is the real
one. Fake runners must allocate output buffers with `malloc`, because `prk_release_process_result()` frees them.

## Implementation

| File | Role |
| --- | --- |
| `src/process/prk_process.c` | Portable and pure: validation, defaults, bounded output buffers, the environment merge, Windows quoting. |
| `src/process/prk_process_windows.c` | `CreateProcessW` (suspended, `CREATE_NO_WINDOW`, only the three pipe handles inherited via `PROC_THREAD_ATTRIBUTE_HANDLE_LIST`), job object with `JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE`, one thread per pipe, `GetTickCount64` deadline. |
| `src/process/prk_process_posix.c` | `fork`/`execve` in a new process group, `poll` with a `CLOCK_MONOTONIC` deadline, exec errors reported over a close-on-exec pipe. Linux and macOS are not supported spawn hosts yet (#34, #35); this adapter lets the box builds and sanitizers exercise the layer. |

This is a practical first control, not an OS sandbox: the child runs with the caller's user rights. Job-object
hardening beyond kill-on-close (memory or CPU limits, UI restrictions) can be added later.

## Tests

`proofrunkit_selftest` covers validation, the environment merge, quoting, and bounded output as pure functions, and
spawns `prk_spawn_child` (a small test program built next to the self-test, never installed) for success, a non-zero
exit with both streams, environment and working directory, a 200 KB stdin, a timeout, a missing program and a missing
directory, and the output limit.
