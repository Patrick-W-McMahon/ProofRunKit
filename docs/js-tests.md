# JavaScript test scripts

ProofRunKit runs kit-authored JavaScript tests on an embedded, sandboxed **QuickJS-ng 0.18.0**
([#11](https://github.com/Patrick-W-McMahon/ProofRunKit/issues/11)), linked statically into the single executable.
Test files are read at run time, so the executable is never rebuilt when a test changes.

```text
prk run tests/js/prk_js_api_test.js
prk run --tag fast --timeout 5000 suite.lua suite.js      # Lua and JavaScript scripts in one run
prk list tests/js/prk_js_api_test.js
```

Files ending in `.js` or `.mjs` load through the JavaScript engine. Each file is evaluated as a classic script in its
own QuickJS-ng runtime; ES module syntax (`import` / `export`) is not available because scripts cannot load other
files.

## Example

```js
prk.describe('math', () => {
    let values;
    prk.beforeEach(() => { values = [3, 1, 2]; });

    prk.addTest('sort', () => {
        values.sort();
        prk.expectDeepEqual(values, [1, 2, 3]);
    }, { tags: ['fast'] });

    prk.addTest('async work', async () => {
        const total = await Promise.resolve(values.reduce((a, b) => a + b, 0));
        prk.expectEqual(total, 6);
    }, { timeoutMs: 1000 });
});
```

## The `prk` object

The API is the camelCase twin of the Lua API in [lua-tests.md](lua-tests.md#lua-and-javascript-names); suites,
hooks, selection, and execution order are the same engine-neutral code, so both languages behave identically.

| Function | Behavior |
| --- | --- |
| `prk.addTest(name, body, {tags, timeoutMs, skip})` | Registers a test. `body` may be `async` or return a promise; the test waits for it. Unknown option keys are an error. `skip: true` or `skip: 'reason'` reports the test as skipped. |
| `prk.describe(name, define, {tags})` | Groups tests; names become `suite.test`. Suites nest (at most 31 deep) and their tags are inherited. |
| `prk.beforeAll`, `afterAll`, `beforeEach`, `afterEach` | Suite hooks. Order: `beforeAll` (once, before the suite's first selected test), `beforeEach` root to leaf, the body, `afterEach` leaf to root (always), `afterAll` after the suite's last selected test. |
| `prk.failTest(message)` / `prk.skipTest(reason)` | Throw a `prk.AssertionError` / `prk.Skip`. |
| `prk.readClockMs()`, `prk.measureElapsedMs(fn, ...args)` | Monotonic clock in milliseconds. |
| `prk.isFilePresent(path)`, `prk.readTextFile(path)` | Read-only, relative paths inside the working directory. |
| `prk.runProcess({program, args, cwd, env, inheritEnv, stdin, timeoutMs, outputLimit})` | Starts a program (no shell) and returns `{ok, outcome, exitCode, signal, stdout, stderr, stdoutTruncated, stderrTruncated, elapsedMs, error}`. Same rules as `prk.run_process` in [lua-tests.md](lua-tests.md#processes): a missing program or a timeout is returned, not thrown, and the timeout never outlasts the test. |
| `prk.compile({sources, output, kind, language, compiler, program, archiver, msvcArch, std, includes, defines, libraries, libraryDirs, flags, werror, cwd, timeoutMs})` | Compiles C or C++ with gcc, clang, or `cl` (found with vswhere when not on PATH) into an executable, object, static library, or shared library, and returns the `prk.runProcess` fields plus `diagnostics`, `errors`, `warnings`, `output`, `kind`, `msvcDiscovered`, and `command`. See [c-cpp-driver.md](c-cpp-driver.md). |
| `prk.assemble({source, output, assembler, program, includes, defines, flags, werror, format, msvcArch, cwd, timeoutMs})` | Assembles one source into an object file with `ml64` / `ml` (found with vswhere when not on PATH), GNU `as`, or NASM, and returns the `prk.compile` fields with `assembler` instead of `compiler`. See [asm-driver.md](asm-driver.md). |
| `prk.expectExitCode(result, code [, message])` | Fails with the exit code (or why the program did not run) and its stderr. |
| `print(...)`, `console.log(...)` | Write a line to the console (to stderr when stdout carries a JSON/JUnit report or a listing). |
| `prk.version`, `prk.jsEngine` | `"0.1.0"`, `"QuickJS-ng 0.18.0"`. |

Registration functions may only be called while the script loads, not inside a test.

### Assertions

Every assertion takes an optional final `message` and throws a `prk.AssertionError` whose message starts with
`file:line:` of the calling line, for example `suite.js:12: values differ: expected 3, got 2`.

| Assertion | Passes when |
| --- | --- |
| `expectTrue(v)` / `expectFalse(v)` | `v` is truthy / falsy. |
| `expectEqual(a, b)` / `expectNotEqual(a, b)` | `a === b` / `a !== b`. |
| `expectNullish(v)` / `expectNotNullish(v)` | `v` is / is not `null` or `undefined` (Lua: `expect_nil`). |
| `expectNear(a, b, tolerance)` | `Math.abs(a - b) <= tolerance`. |
| `expectLessThan`, `expectLessOrEqual`, `expectGreaterThan`, `expectGreaterOrEqual` | The comparison holds. |
| `expectContains(container, item)` | Substring of a string, element of an array or `Set`, or a value of an object. |
| `expectMatch(text, pattern)` | `pattern` (a `RegExp` or a regular-expression string) matches. |
| `expectType(v, name)` | `typeof v === name`. |
| `expectDeepEqual(a, b)` | Structural equality of arrays and objects (cycles allowed); the message names the first differing path, e.g. `at value["a"][1]`. |
| `expectError(action, pattern)` | `action()` throws; when given, `pattern` (a `RegExp` or a substring) matches the error message. Returns the message. |
| `expectFilePresent(path)`, `expectFileContains(path, text)` | File checks through the read-only file access. |

Assertion failures and skips are ordinary exceptions, so `try` / `catch` can inspect them
(`error instanceof prk.AssertionError`). A test fails only if one escapes. Any other uncaught exception fails the
test with its message and stack.

## Sandbox

The sandbox is a safety aid for honest test code, not a security boundary for hostile code (see
[DISCLAIMER.md](../DISCLAIMER.md)).

| Control | Behavior |
| --- | --- |
| Processes | Only through `prk.runProcess` (granted by the front end); time-limited, killed with their children at the limit, not sandboxed. |
| Modules and ambient access | Only the ECMAScript built-ins plus `prk`, `print`, and `console.log`. QuickJS-ng's `qjs:std` / `qjs:os` library (`quickjs-libc.c`) is not compiled into the executable, there is no module loader (`import` fails), and there is no `require`, `process`, `setTimeout`, `fetch`, file, or environment access. `Atomics.wait` cannot block. |
| Memory | Each script's runtime may hold at most 64 MiB (`JS_SetMemoryLimit`). Exceeding it fails the test with `memory limit of ... bytes exceeded`; the next test starts after a garbage collection. |
| Instructions | An interrupt handler charges 10,000 operations per call. Loading a script, and each test (hooks and body together), may use at most 200 million. Exceeding the budget fails the test with `instruction limit of ... instructions exceeded`. The interrupt is uncatchable: `try` / `catch` / `finally` cannot swallow it. |
| Time | Optional per-test wall-clock timeout (`timeoutMs` or `--timeout`), checked in the same interrupt handler, also uncatchable. |
| Stack | The JavaScript stack is limited to 512 KiB, half of the default 1 MiB Windows thread stack; deep recursion fails the test with `RangeError: Maximum call stack size exceeded`. |
| Promises | Promise jobs run after every call. A test whose returned promise is still pending when no jobs are left fails with `the returned promise never settled` (there are no timers or IO to wait for). |

`Math.random` and `Date.now()` are not deterministic between runs; tests must not depend on them.

## Errors and exit codes

A syntax error or an exception in top-level code is a load error, reported as `file:line: SyntaxError: ...` (or the
error and its stack) with exit code 4; no tests run. See [exit-codes.md](exit-codes.md) and
[report-formats.md](report-formats.md).

## C ABI

[`include/proofrunkit/prk_js.h`](../include/proofrunkit/prk_js.h): `prk_create_js_script`,
`prk_load_js_test_script`, and `prk_destroy_js_script`, with the same `PrkScriptLimits`, `PrkScriptHost`, and
`PrkScriptRunSettings` as the Lua engine. The implementation is `src/script/prk_js_script.c` (runtime, budgets,
engine callbacks) and `src/script/prk_js_api.c` (the `prk` object and the assertion prelude). The vendored engine and
what is left out of it are described in
[`third_party/quickjs-ng-0.18.0/PROOFRUNKIT.md`](../third_party/quickjs-ng-0.18.0/PROOFRUNKIT.md).

## Coverage

`tests/js/prk_js_api_test.js` runs through `prk` in CTest (`prk_js_api_selftest`), `tests/js/process_test.js`
(`prk_js_process_test`) starts `prk` itself with `prk.runProcess`, and `tests/js/samples/` covers a
failing test, a syntax error, and an endless loop that tries to catch the interrupt. The C++ self-test suite `js.*`
covers the sandbox, budgets, async tests, assertions, suites, tags, and files.

Self-hosted (#55): `tests/js/self/sandbox_test.js` (`prk_self_sandbox_test`) checks the sandbox, the module loader,
and versions; `tests/js/self/cli_test.js.in` (`prk_js_cli_test`) runs `./prk` to check the whole command line and
runner. [`self-hosting.md`](self-hosting.md) maps every C++ case to its prk equivalent.
