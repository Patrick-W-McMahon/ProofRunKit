# Writing ProofRunKit tests in Lua

ProofRunKit embeds Lua 5.4.9 ([#9](https://github.com/Patrick-W-McMahon/ProofRunKit/issues/9)). A Lua test script is
an ordinary `.lua` file that registers tests when it loads. The executable reads the file at run time, so changing a
test never requires rebuilding `prk.exe` / `proofrunkit.exe`
([#22](https://github.com/Patrick-W-McMahon/ProofRunKit/issues/22)).

```lua
-- tests/math_test.lua
prk.add_test("math.adds_small_numbers", function()
    prk.expect_equal(2 + 2, 4)
end)

prk.add_test("math.compares", function()
    prk.expect_true(3 > 2, "three is greater than two")
end)
```

```powershell
prk run tests\math_test.lua                # run every test in the file
prk run --filter adds tests\math_test.lua  # only tests whose name contains "adds"
prk run --json tests\*.lua                 # JSON report (print output goes to stderr)
prk list tests\math_test.lua               # list test names without running them
```

Several scripts can be passed at once. Each script gets its own Lua state, so globals in one script are not visible
in another. Test names must be unique across all scripts in a run.

## The `prk` table

The test-authoring API ([#12](https://github.com/Patrick-W-McMahon/ProofRunKit/issues/12)) is engine-neutral: the
JavaScript engine ([#11](https://github.com/Patrick-W-McMahon/ProofRunKit/issues/11)) exposes the same functions with
camelCase names (see [Lua and JavaScript names](#lua-and-javascript-names)). Processes are started with
`prk.run_process` over the spawn layer ([#13](https://github.com/Patrick-W-McMahon/ProofRunKit/issues/13)).

### Registering tests

Registration functions may only be called while the script loads (top-level code or inside `prk.describe`), not
inside a test or hook.

| Function | Behavior |
| --- | --- |
| `prk.add_test(name, body [, options])` | Registers a test. `options` is a table with any of `tags = {"fast", ...}`, `timeout_ms = 500` (wall-clock limit for this test), and `skip = "reason"` (or `skip = true`) to register the test as skipped without running it. Unknown option names are errors, so typos are caught. |
| `prk.describe(name, define [, options])` | Groups tests into a suite: `define` runs immediately and the tests it adds are named `suite.test` (nested suites give `outer.inner.test`). `options.tags` are inherited by every test in the suite. Suite names may not contain `.`. |
| `prk.before_all(hook)` | Runs once before the first selected test of the enclosing suite (or of the script, at top level). If it fails, every test in that suite fails with `before_all failed: ...`. |
| `prk.after_all(hook)` | Runs once after the last selected test of the suite. A failure is reported on that last test. |
| `prk.before_each(hook)` | Runs before every test in the suite, outermost suite first. If it fails, the body is not run. |
| `prk.after_each(hook)` | Runs after every test in the suite, innermost suite first, even when the test failed. |

Hooks only run for suites that have at least one selected test. For one test the order is: pending `before_all`
hooks (outermost first), `before_each` hooks (outermost first), the body, `after_each` hooks (innermost first), then
`after_all` hooks of every suite this was the last test of. Several hooks of one kind in one suite run in the order
they were added.

```lua
prk.describe("parser", function()
    local input
    prk.before_each(function() input = { "a", "b" } end)

    prk.add_test("reads_tokens", function()
        prk.expect_deep_equal(input, { "a", "b" })
    end, { tags = { "fast" } })

    prk.add_test("handles_huge_files", function()
        -- ...
    end, { tags = { "slow" }, timeout_ms = 5000 })
end)
```

### Assertions

Every assertion fails the test with a message that starts with `file:line:` of the calling line. An optional last
`message` argument is used as a prefix (or, for the one-value checks, instead of the default text).

| Function | Passes when |
| --- | --- |
| `prk.expect_true(value [, message])` | `value` is truthy (not `nil` or `false`). |
| `prk.expect_false(value [, message])` | `value` is `nil` or `false`. |
| `prk.expect_equal(actual, expected [, message])` | `actual == expected`. The message shows both values (strings quoted). |
| `prk.expect_not_equal(actual, unexpected [, message])` | `actual ~= unexpected`. |
| `prk.expect_nil(value [, message])` / `prk.expect_not_nil(value [, message])` | `value` is / is not `nil`. |
| `prk.expect_near(actual, expected, tolerance [, message])` | `abs(actual - expected) <= tolerance` (numbers only). |
| `prk.expect_less_than(a, b [, message])` / `prk.expect_less_or_equal(a, b [, message])` | `a < b` / `a <= b`. |
| `prk.expect_greater_than(a, b [, message])` / `prk.expect_greater_or_equal(a, b [, message])` | `a > b` / `a >= b`. |
| `prk.expect_contains(container, item [, message])` | `container` is a string containing `item` (plain text, not a pattern), or a table with a value equal to `item`. |
| `prk.expect_match(text, pattern [, message])` | `text` is a string matching the Lua pattern. |
| `prk.expect_type(value, type_name [, message])` | `type(value) == type_name`. |
| `prk.expect_deep_equal(actual, expected [, message])` | Tables have the same keys and recursively equal values (cycles are handled). The message names the first difference, e.g. `at value["a"][2]: expected 3, got 2`. |
| `prk.expect_error(action [, pattern [, message]])` | Calling `action()` raises an error (whose text matches `pattern`, when given). Returns the error text. |
| `prk.expect_file_present(path [, message])` | `prk.is_file_present(path)` is true. |
| `prk.expect_file_contains(path, text [, message])` | The file's content contains `text` (plain text). |

| Function | Behavior |
| --- | --- |
| `prk.fail_test([message])` | Fails the test. |
| `prk.skip_test([reason])` | Marks the test skipped (only if it has not already failed). Inside `before_each`, skips the test. |

Failures and skips are error objects (`prk.AssertionError` and `prk.Skip`; `tostring` gives the message), so they stop
the test like any error, and `pcall` can catch them, exactly like a thrown error in JavaScript. A failure only counts
when the error leaves the test. Any other Lua error, such as `error("...")` or a failed `assert(...)`, also fails the
test, and the report includes the message and a stack traceback.

### Timing and files

| Function | Behavior |
| --- | --- |
| `prk.read_clock_ms()` | Monotonic clock in milliseconds (an integer). Only differences are meaningful. |
| `prk.measure_elapsed_ms(action, ...)` | Calls `action(...)` and returns the elapsed milliseconds. |
| `prk.is_file_present(path)` | `true` when `path` is an existing regular file. |
| `prk.read_text_file(path)` | The whole file as a string (bytes unchanged). The file must fit in the script's memory limit. |

File access is read-only and limited to relative paths inside the working directory: absolute paths, drive letters,
`:` (alternate data streams), and `..` components are refused with `access denied`. Symbolic links are not resolved,
so this is a guard against mistakes, not a security boundary.

### Processes

```lua
local result = prk.run_process{program = "gcc", args = {"-c", "hello.c", "-o", "hello.o"}, timeout_ms = 30000}
prk.expect_exit_code(result, 0)        -- fails with the exit code (or why it did not run) and stderr
local run = prk.run_process{program = "./hello", stdin = "input\n"}
prk.expect_equal(run.stdout, "expected output\n")
```

`prk.run_process{...}` starts a program directly (never through a shell), waits for it, and returns a table.

| Option | Meaning |
| --- | --- |
| `program` | Required. A path, or a bare name found on `PATH` (Windows adds `.exe`). |
| `args` | Sequence of argument strings, passed exactly (spaces and quotes need no escaping). |
| `cwd` | Working directory; default: ProofRunKit's. |
| `env` | `{NAME = "value"}` added to (or, with `inherit_env = false`, replacing) ProofRunKit's environment. |
| `stdin` | Bytes written to the program's standard input. |
| `timeout_ms` | Default 60000. Never longer than the time left in the running test; at the limit the program and its children are killed. |
| `output_limit` | Bytes kept per stream (default 16 MiB); the rest is dropped and flagged. |

| Result field | Value |
| --- | --- |
| `ok` | `true` when the program exited with code 0. |
| `outcome` | `"exited"`, `"timed out"`, `"failed to start"`, or `"killed by a signal"` (POSIX). |
| `exit_code` | Exit code when `outcome == "exited"`, else `nil`. |
| `stdout`, `stderr` | Captured output (strings); `stdout_truncated` / `stderr_truncated` flag the output limit. |
| `elapsed_ms` | Wall-clock time of the run. |
| `error` | Why it failed to start, e.g. `executable not found: gcc`. |

A missing program or a timeout is returned, not raised, so a test can check for it; `prk.expect_exit_code(result,
code [, message])` turns any unexpected result into a failure such as
`build.lua:4: unexpected process result: expected exit code 0, got exit code 1` followed by the program's stderr.

### Compiling C and C++

`prk.compile{sources = {...}, output = ..., kind = "executable" | "object" | "static_library" | "shared_library",
language = "c" | "c++", compiler = "auto" | "gcc" | "clang" | "msvc", std = ..., includes = {...}, defines = {...},
libraries = {...}, library_dirs = {...}, flags = {...}, werror = true}` compiles with a real toolchain (MSVC is found
with vswhere when `cl` is not on PATH) and returns the `prk.run_process` fields plus `diagnostics`, `errors`,
`warnings`, `output`, `kind`, `msvc_discovered`, and `command`; see
[c-cpp-driver.md](c-cpp-driver.md) ([#14](https://github.com/Patrick-W-McMahon/ProofRunKit/issues/14)).

### Assembling

`prk.assemble{source = ..., output = ..., assembler = "auto" | "masm" | "gas" | "nasm", program = ..., includes = {...},
defines = {...}, flags = {...}, format = ..., msvc_arch = ..., werror = true}` assembles one source into an object
file with `ml64` / `ml` (found with vswhere when not on PATH), GNU `as`, or NASM, and returns the `prk.compile` fields
with `assembler` instead of `compiler`; link the object with `prk.compile`. See
[asm-driver.md](asm-driver.md) ([#16](https://github.com/Patrick-W-McMahon/ProofRunKit/issues/16)).

### Other fields

| Field | Value |
| --- | --- |
| `prk.version` | ProofRunKit version string, e.g. `"0.1.0"`. |
| `prk.lua_version` | Embedded Lua version, e.g. `"Lua 5.4.9"`. |

`print(...)` writes to the console (stdout) for console reports, and to stderr for `--json` and `list`, so it never
corrupts machine-readable output.

## Selecting tests and timeouts

```powershell
prk run --tag fast tests\*.lua                 # only tests tagged "fast" (repeat --tag for "any of")
prk run --exclude-tag slow tests\*.lua         # everything except tests tagged "slow" (wins over --tag)
prk run --filter parser. tests\*.lua           # only tests whose full name contains "parser."
prk run --timeout 2000 tests\*.lua             # default wall-clock limit per test, in milliseconds
```

Selection happens while scripts load: tests that are not selected are never registered, so they are not listed, not
run, and their suites' hooks do not run for them. A test's own `timeout_ms` wins over `--timeout`. A timed-out test
fails with `timeout of N ms exceeded`; as with the instruction budget, `pcall` cannot swallow it. The timeout covers
the test's hooks and body together. `--tag`, `--exclude-tag`, and `--timeout` apply only to test scripts.

## Lua and JavaScript names

The JavaScript engine ([#11](https://github.com/Patrick-W-McMahon/ProofRunKit/issues/11), see
[js-tests.md](js-tests.md)) exposes the same API, with camelCase names and option keys:

| Lua | JavaScript |
| --- | --- |
| `prk.add_test(name, body, {tags=, timeout_ms=, skip=})` | `prk.addTest(name, body, {tags, timeoutMs, skip})` |
| `prk.describe(name, define, {tags=})` | `prk.describe(name, define, {tags})` |
| `prk.before_all` / `after_all` / `before_each` / `after_each` | `prk.beforeAll` / `afterAll` / `beforeEach` / `afterEach` |
| `prk.expect_true`, `prk.expect_equal`, `prk.expect_deep_equal`, ... | `prk.expectTrue`, `prk.expectEqual`, `prk.expectDeepEqual`, ... |
| `prk.expect_nil`, `prk.expect_not_nil` | `prk.expectNullish`, `prk.expectNotNullish` |
| `prk.expect_error(action, pattern)` | `prk.expectError(action, regexp or substring)` |
| `prk.fail_test`, `prk.skip_test` | `prk.failTest`, `prk.skipTest` |
| `prk.read_clock_ms`, `prk.measure_elapsed_ms` | `prk.readClockMs`, `prk.measureElapsedMs` |
| `prk.is_file_present`, `prk.read_text_file` | `prk.isFilePresent`, `prk.readTextFile` |
| `prk.run_process{program=, args=, cwd=, env=, inherit_env=, stdin=, timeout_ms=, output_limit=}` | `prk.runProcess({program, args, cwd, env, inheritEnv, stdin, timeoutMs, outputLimit})` |
| result `exit_code`, `elapsed_ms`, `stdout_truncated` | result `exitCode`, `elapsedMs`, `stdoutTruncated` |
| `prk.expect_exit_code(result, code)` | `prk.expectExitCode(result, code)` |
| `prk.compile{..., library_dirs=, msvc_arch=, timeout_ms=}` | `prk.compile({..., libraryDirs, msvcArch, timeoutMs})` |
| compile result `msvc_discovered` | compile result `msvcDiscovered` |

## Sandbox

Test scripts run in a restricted Lua environment. The sandbox is a safety aid for honest test code, not a security
boundary for hostile code (see [DISCLAIMER.md](../DISCLAIMER.md)).

| Control | Behavior |
| --- | --- |
| Libraries | Only `base`, `coroutine`, `table`, `string`, `math`, and `utf8`. The `io`, `os`, `package`, and `debug` libraries are not compiled into the executable at all. |
| Removed functions | `dofile`, `loadfile`, `require`, and `string.dump`. |
| `load` | Text chunks only; precompiled bytecode is rejected (also for the script file itself). |
| Memory | Each script's Lua state may hold at most 64 MiB. Exceeding it fails the current test with `memory limit of ... bytes exceeded`; the next test starts after a garbage collection. |
| Instructions | Loading a script, and each test (its hooks and body together), may run at most 200 million Lua VM instructions. Exceeding the budget fails the test with `instruction limit of ... instructions exceeded`. Catching that error with `pcall` does not help: the script is stopped again at its next instruction. |
| Time | Optional per-test wall-clock timeout (`timeout_ms` or `--timeout`), checked every 1000 instructions. |
| Files | Read-only, relative paths inside the working directory, through `prk.is_file_present` and `prk.read_text_file` only. |
| Processes | Only through `prk.run_process`, which the ProofRunKit front end grants. Programs run with the user's rights and are not sandboxed; they are always time-limited and killed with their children at the limit. |

Long-running work inside a single standard-library C function (for example one enormous `string.rep`) is bounded by
the memory limit rather than the instruction budget or the timeout, because both are checked between VM instructions.

`pairs` iteration order and `math.random` are not deterministic between runs; tests must not depend on them.

## Exit codes and reports

See [exit-codes.md](exit-codes.md) for the exit-code contract and [report-formats.md](report-formats.md) for the
console, JSON, and JUnit XML formats.

## C ABI

The engine is exposed to the C/C++ core through [`include/proofrunkit/prk_lua.h`](../include/proofrunkit/prk_lua.h):
`prk_create_lua_script`, `prk_load_lua_test_script`, and `prk_destroy_lua_script`. The engine-neutral pieces shared
with the JavaScript engine are in [`include/proofrunkit/prk_script.h`](../include/proofrunkit/prk_script.h):
`PrkScriptLimits`, `PrkScriptRunSettings` (name filter, tags, default timeout), and `PrkScriptHost`, the callbacks for
`print`, the clock, and read-only file access. The engine performs no IO of its own; the front end reads the script,
passes its text in, and supplies the host callbacks. Suites, hooks, selection, and the per-test execution order live
in the engine-neutral test plan (`src/script/prk_test_plan.c`), so both engines behave the same.

## Self-hosted tests

ProofRunKit tests itself with its own Lua platform
([#55](https://github.com/Patrick-W-McMahon/ProofRunKit/issues/55)). CTest runs these scripts through the shipped `prk`
executable on every build:

| Script | CTest | Covers |
| --- | --- | --- |
| [`tests/lua/prk_lua_api_test.lua`](../tests/lua/prk_lua_api_test.lua) | `prk_lua_api_selftest` | Versions, skips, and the sandbox (no `io`/`os`/`package`/`debug`, text-only `load`, safe libraries). |
| [`tests/lua/self/assertions_test.lua`](../tests/lua/self/assertions_test.lua) | `prk_self_assertions_test` | Every `prk.expect_*` assertion, passing and failing, including exact failure messages, `file:line:` locations, and catchable error objects. |
| [`tests/lua/self/suites_and_hooks_test.lua`](../tests/lua/self/suites_and_hooks_test.lua) | `prk_self_suites_and_hooks_test` | Suites, the order of the four hooks, shared fixtures, and registration-only-while-loading rules. |
| [`tests/lua/self/timing_and_files_test.lua`](../tests/lua/self/timing_and_files_test.lua) | `prk_self_timing_and_files_test` | The clock, `measure_elapsed_ms`, file checks against the source tree, and refusal of paths outside the working directory. |
| [`tests/lua/self/tags_test.lua`](../tests/lua/self/tags_test.lua) | `prk_self_tags_test` | `--tag` / `--exclude-tag` selection, tag inheritance, the skip option, and hooks of unselected suites not running. |
| [`tests/lua/self/process_test.lua`](../tests/lua/self/process_test.lua) | `prk_self_process_test` | `prk.run_process` starting `prk` itself: stdout, a non-zero exit with stderr, exact arguments, and a missing program as a result. |
| [`tests/lua/self/compile_test.lua.in`](../tests/lua/self/compile_test.lua.in) | `prk_self_compile_test` | `prk.compile` on the samples in `tests/drivers/c`: C and C++ programs built and run, warnings, `werror`, parsed errors; skips without a compiler. |
| [`tests/lua/self/reports_test.lua`](../tests/lua/self/reports_test.lua) | `prk_self_reports_test` (after `prk_self_reports_fixture` and `prk_self_reports_builtin`) | The runner and reporters, end to end. The fixture runs write JSON and JUnit files with `--filter`, `--exclude-tag`, `--json-file`, and `--junit-file`. The script then checks totals, order, outcomes, name mapping, escaping, and the built-in cycle-counter checks. |

Samples under `tests/lua/samples/` show that failures, syntax errors, missing files, endless loops (instruction budget
and `--timeout`) are reported correctly.

The C++ harness `proofrunkit_selftest` stays in place as the fallback (#27). Some of its cases cover the C ABI
directly (registry growth and duplicate names, invalid arguments, the memory limit, parser edge cases). Scripts can
now reach command-line behavior through `prk.run_process` (#13), but not the C ABI itself. The harness can only be retired once the
self-hosted suite covers every case, is green on Windows x64 and x86, and Patrick approves.
