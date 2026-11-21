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

This is the first, minimal part of the test-authoring API. The full API (process spawn, file checks, timing, and
fixtures) is tracked in [#12](https://github.com/Patrick-W-McMahon/ProofRunKit/issues/12).

| Function | Behavior |
| --- | --- |
| `prk.add_test(name, body)` | Registers a test. Only allowed while the script loads (top-level code), not inside a test. |
| `prk.expect_true(value [, message])` | Fails the test and stops its body unless `value` is truthy. |
| `prk.expect_equal(actual, expected [, message])` | Fails the test and stops its body unless `actual == expected`. The message shows both values. |
| `prk.fail_test([message])` | Fails the test and stops its body. |
| `prk.skip_test([reason])` | Marks the test skipped and stops its body. |
| `prk.version` | ProofRunKit version string, e.g. `"0.1.0"`. |
| `prk.lua_version` | Embedded Lua version, e.g. `"Lua 5.4.9"`. |

Any other Lua error inside a test body, such as `error("...")` or a failed `assert(...)`, also fails the test, and the
report includes the message and a stack traceback. Failure messages start with `file:line:`.

`print(...)` writes to the console (stdout) for console reports, and to stderr for `--json` and `list`, so it never
corrupts machine-readable output.

## Sandbox

Test scripts run in a restricted Lua environment. The sandbox is a safety aid for honest test code, not a security
boundary for hostile code (see [DISCLAIMER.md](../DISCLAIMER.md)).

| Control | Behavior |
| --- | --- |
| Libraries | Only `base`, `coroutine`, `table`, `string`, `math`, and `utf8`. The `io`, `os`, `package`, and `debug` libraries are not compiled into the executable at all. |
| Removed functions | `dofile`, `loadfile`, `require`, and `string.dump`. |
| `load` | Text chunks only; precompiled bytecode is rejected (also for the script file itself). |
| Memory | Each script's Lua state may hold at most 64 MiB. Exceeding it fails the current test with `memory limit of ... bytes exceeded`; the next test starts after a garbage collection. |
| Instructions | Loading a script, and each test body, may run at most 200 million Lua VM instructions. Exceeding the budget fails the test with `instruction limit of ... instructions exceeded`. Catching that error with `pcall` does not help: the script is stopped again at its next instruction. |

Long-running work inside a single standard-library C function (for example one enormous `string.rep`) is bounded by
the memory limit rather than the instruction budget. Wall-clock timeouts arrive with the process-spawn layer
([#13](https://github.com/Patrick-W-McMahon/ProofRunKit/issues/13)).

`pairs` iteration order and `math.random` are not deterministic between runs; tests must not depend on them.

## Exit codes

| Code | Meaning |
| --- | --- |
| 0 | Every selected test passed (skips do not fail a run). |
| 1 | At least one test failed. |
| 2 | Usage error, including an unsupported script extension. |
| 3 | A requested feature is not implemented yet, for example JavaScript test scripts ([#11](https://github.com/Patrick-W-McMahon/ProofRunKit/issues/11)). |
| 4 | A test script could not be read, compiled, or loaded (missing file, syntax error, error in top-level code, duplicate test name). Nothing is run. |

## C ABI

The engine is exposed to the C/C++ core through [`include/proofrunkit/prk_lua.h`](../include/proofrunkit/prk_lua.h):
`prk_create_lua_script`, `prk_load_lua_test_script`, and `prk_destroy_lua_script`. The engine performs no file IO;
the front end reads the script and passes its text in.

## Self-hosted tests

[`tests/lua/prk_lua_api_test.lua`](../tests/lua/prk_lua_api_test.lua) is ProofRunKit testing its own Lua API and
sandbox through the shipped executable (a first step of
[#55](https://github.com/Patrick-W-McMahon/ProofRunKit/issues/55)). CTest runs it, plus samples under
`tests/lua/samples/` that prove failures, syntax errors, missing files, and endless loops are reported correctly.
