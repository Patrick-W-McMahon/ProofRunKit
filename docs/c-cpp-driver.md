# C and C++ compiler driver

Status: first slice of [#14](https://github.com/Patrick-W-McMahon/ProofRunKit/issues/14). ProofRunKit compiles C and
C++ with a real toolchain (gcc, clang, or MSVC `cl`), parses the compiler's diagnostics, and lets a Lua or JavaScript
test run the produced program with `prk.run_process` / `prk.runProcess`. It never compiles in-process.

- C API: [`include/proofrunkit/prk_c_driver.h`](../include/proofrunkit/prk_c_driver.h)
  (`prk_compile_program`, `prk_parse_compiler_diagnostic`).
- Sources: [`src/drivers/prk_c_driver.c`](../src/drivers/prk_c_driver.c) (command lines, compile, AUTO fallback),
  [`src/drivers/prk_c_diagnostics.c`](../src/drivers/prk_c_diagnostics.c) (parsers).
- Script bindings: `prk.compile` in [`src/script/prk_lua_compile.c`](../src/script/prk_lua_compile.c) and
  [`src/script/prk_js_compile.c`](../src/script/prk_js_compile.c).
- Everything runs through the injectable `PrkProcessRunner` of the spawn layer ([process-spawn.md](process-spawn.md)),
  so compiles have the same timeouts, tree kill, and output limits as any other process.

## From a test script

```lua
prk.add_test("hello.prints_greeting", function()
    local build = prk.compile{sources = {"src/hello.c"}, output = "build/hello", std = "c17", werror = true}
    prk.expect_exit_code(build, 0)            -- fails with the exit code and the compiler's stderr
    local run = prk.run_process{program = build.output}
    prk.expect_exit_code(run, 0)
    prk.expect_equal(run.stdout, "hello\n")
end)
```

```js
prk.addTest('hello.printsGreeting', () => {
    const build = prk.compile({sources: ['src/hello.c'], output: 'build/hello', std: 'c17', werror: true});
    prk.expectExitCode(build, 0);
    const run = prk.runProcess({program: build.output});
    prk.expectExitCode(run, 0);
});
```

| Option (Lua / JavaScript) | Meaning |
| --- | --- |
| `sources` | Required. A path or a sequence of paths. |
| `output` | Required. Program to produce; on Windows `.exe` is added when the file name has no extension. |
| `language` | `"c"` (default) or `"c++"`; picks `g++` / `clang++` and adds `/EHsc` for `cl`. |
| `compiler` | `"auto"` (default), `"gcc"`, `"clang"`, or `"msvc"`. |
| `program` | Compiler program override, e.g. `"x86_64-w64-mingw32-gcc"` or `"clang-18"`; needs a `compiler` family. |
| `std` | Language standard, e.g. `"c17"`, `"c++20"` (`-std=` / `/std:`). |
| `includes`, `defines`, `flags` | Include folders (`-I` / `/I`), macros (`-D` / `/D`), and extra flags passed unchanged at the end. |
| `werror` | `true` adds `-Werror` / `/WX`. Warnings are always on: `-Wall -Wextra` or `/W4`. |
| `cwd` | Folder the compiler runs in. |
| `timeout_ms` / `timeoutMs` | Compiler timeout; default 60000, never longer than the time left in the test. |

The result has every `prk.run_process` field (`ok`, `outcome`, `exit_code` / `exitCode`, `stdout`, `stderr`,
`elapsed_ms` / `elapsedMs`, `error`), so `prk.expect_exit_code` works on it, plus:

| Field | Value |
| --- | --- |
| `compiler`, `program` | Family that ran (`"gcc"`, `"clang"`, `"msvc"`) and the program, e.g. `"g++"`. |
| `command` | The full command line, for reports. |
| `output` | Path of the produced program (with `.exe` on Windows). |
| `errors`, `warnings` | Counts of parsed errors and warnings. |
| `diagnostics` | Sequence of `{severity, file, line, column, code, message}`; `severity` is `"error"`, `"warning"`, or `"note"`, `code` is the MSVC code (`C2065`) or the GCC/Clang option (`-Wunused-variable`). At most 1000 are kept; the counts include all. |

A missing compiler is returned, not raised: `outcome` is `"failed to start"` and `error` says why (with AUTO:
`no C/C++ compiler found on PATH (tried gcc, clang)`), so a test can `prk.skip_test` on machines without a toolchain.

## Command lines

| Family | Command |
| --- | --- |
| gcc / clang | `gcc -fdiagnostics-color=never -Wall -Wextra [-Werror] [-std=S] -I... -D... sources -o OUT flags...` (`g++` / `clang++` for C++) |
| MSVC | `cl /nologo /W4 [/WX] [/EHsc] [/std:S] /I... /D... sources /FeOUT flags...` |

AUTO tries, in order, Windows: `cl`, `clang`, `gcc`; elsewhere: `gcc`, `clang`. It moves on only when a compiler
fails to start; a compiler that runs and reports errors is the answer.

## Diagnostics parsing

| Family | Line forms |
| --- | --- |
| GCC, Clang | `file:line:col: error: msg [-Wflag]`, `file:line: warning: msg`, `file:line:col: note: msg`, `cc1: fatal error: msg`, `collect2: error: msg` |
| MSVC | `file(line): error C2065: msg`, `file(line,col): warning C4100: msg`, `file(line): note: msg`, `LINK : fatal error LNK1561: msg`, `cl : command line warning D9002: msg` |

Other lines (source excerpts, "In function" headers, the cl banner) are ignored. Windows paths with drive letters
parse correctly because the location is read backward from the severity token.

## Not yet (follow-up slices of #14)

- MSVC discovery with `vswhere` / `vcvarsall.bat`: today `cl` must be on `PATH` (a Developer Command Prompt).
- Separate compile and link steps, static and shared libraries, and object-file output.
- Per-target defaults (MinGW cross prefixes, `-m32`).

## Tests

- `proofrunkit_selftest`: `c_driver.*` (parsers for gcc, clang, and MSVC lines, command lines, output paths, compiles
  through a fake runner including AUTO fallback, `prk.compile` in Lua and JavaScript, and one real compile that skips
  without a compiler).
- CTest `prk_self_compile_test` and `prk_js_compile_test`: compile the samples in
  [`tests/drivers/c`](../tests/drivers/c), run the programs, and check warnings, `werror`, and parsed errors. Every
  test skips when no compiler is on `PATH`.
