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
camelCase names (see [Lua and JavaScript names](#lua-and-javascript-names)). Process spawn arrives with
[#13](https://github.com/Patrick-W-McMahon/ProofRunKit/issues/13).

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

The JavaScript engine ([#11](https://github.com/Patrick-W-McMahon/ProofRunKit/issues/11)) will expose the same API,
with camelCase names and option keys:

| Lua | JavaScript |
| --- | --- |
| `prk.add_test(name, body, {tags=, timeout_ms=, skip=})` | `prk.addTest(name, body, {tags, timeoutMs, skip})` |
| `prk.describe(name, define, {tags=})` | `prk.describe(name, define, {tags})` |
| `prk.before_all` / `after_all` / `before_each` / `after_each` | `prk.beforeAll` / `afterAll` / `beforeEach` / `afterEach` |
| `prk.expect_true`, `prk.expect_equal`, `prk.expect_deep_equal`, ... | `prk.expectTrue`, `prk.expectEqual`, `prk.expectDeepEqual`, ... |
| `prk.expect_error(action, pattern)` | `prk.expectError(action, regexp)` |
| `prk.fail_test`, `prk.skip_test` | `prk.failTest`, `prk.skipTest` |
| `prk.read_clock_ms`, `prk.measure_elapsed_ms` | `prk.readClockMs`, `prk.measureElapsedMs` |
| `prk.is_file_present`, `prk.read_text_file` | `prk.isFilePresent`, `prk.readTextFile` |

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

Long-running work inside a single standard-library C function (for example one enormous `string.rep`) is bounded by
the memory limit rather than the instruction budget or the timeout, because both are checked between VM instructions.

`pairs` iteration order and `math.random` are not deterministic between runs; tests must not depend on them.

## Exit codes

| Code | Meaning |
| --- | --- |
| 0 | Every selected test passed (skips do not fail a run). |
| 1 | At least one test failed. |
| 2 | Usage error, including an unsupported script extension. |
| 3 | A requested feature is not implemented yet, for example JavaScript test scripts ([#11](https://github.com/Patrick-W-McMahon/ProofRunKit/issues/11)). |
| 4 | A test script could not be read, compiled, or loaded (missing file, syntax error, error in top-level code, duplicate test name, bad test options). Nothing is run. |

## C ABI

The engine is exposed to the C/C++ core through [`include/proofrunkit/prk_lua.h`](../include/proofrunkit/prk_lua.h):
`prk_create_lua_script`, `prk_load_lua_test_script`, and `prk_destroy_lua_script`. The engine-neutral pieces shared
with the JavaScript engine are in [`include/proofrunkit/prk_script.h`](../include/proofrunkit/prk_script.h):
`PrkScriptLimits`, `PrkScriptRunSettings` (name filter, tags, default timeout), and `PrkScriptHost`, the callbacks for
`print`, the clock, and read-only file access. The engine performs no IO of its own; the front end reads the script,
passes its text in, and supplies the host callbacks. Suites, hooks, selection, and the per-test execution order live
in the engine-neutral test plan (`src/script/prk_test_plan.c`), so both engines behave the same.

## Self-hosted tests

[`tests/lua/prk_lua_api_test.lua`](../tests/lua/prk_lua_api_test.lua) is ProofRunKit testing its own Lua API and
sandbox through the shipped executable (a first step of
[#55](https://github.com/Patrick-W-McMahon/ProofRunKit/issues/55)). CTest runs it, plus samples under
`tests/lua/samples/` that prove failures, syntax errors, missing files, and endless loops are reported correctly.
