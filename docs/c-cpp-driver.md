# C and C++ compiler driver

Status: done ([#14](https://github.com/Patrick-W-McMahon/ProofRunKit/issues/14)). ProofRunKit compiles C and C++ with
a real toolchain (gcc, clang, or MSVC `cl`, found on PATH or, for MSVC, with vswhere and vcvarsall), builds
executables, object files, static libraries, and shared libraries, parses the diagnostics, and lets a Lua or
JavaScript test run the produced program with `prk.run_process` / `prk.runProcess`. It never compiles in-process.

- C API: [`include/proofrunkit/prk_c_driver.h`](../include/proofrunkit/prk_c_driver.h)
  (`prk_compile_program`, `prk_parse_compiler_diagnostic`).
- MSVC discovery: [`include/proofrunkit/prk_msvc_toolchain.h`](../include/proofrunkit/prk_msvc_toolchain.h)
  (`prk_discover_msvc_toolchain`).
- Sources: [`src/drivers/prk_c_driver.c`](../src/drivers/prk_c_driver.c) (compile, AUTO and MSVC fallback),
  [`src/drivers/prk_c_arguments.c`](../src/drivers/prk_c_arguments.c) (command lines),
  [`src/drivers/prk_c_diagnostics.c`](../src/drivers/prk_c_diagnostics.c) (parsers),
  [`src/drivers/prk_msvc_toolchain.c`](../src/drivers/prk_msvc_toolchain.c) (vswhere / vcvarsall).
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
| `sources` | Required. A path or a sequence of paths: C/C++ sources, and object files or libraries to link in. |
| `output` | Required. File to produce; for executables on Windows `.exe` is added when the file name has no extension. |
| `kind` | `"executable"` (default), `"object"` (compile one source only: `-c` / `/c`), `"static_library"` (archive object files with `ar rcs` / `lib /OUT:`), or `"shared_library"` (`-shared` (+`-fPIC` off Windows) / `/LD`). |
| `libraries`, `library_dirs` / `libraryDirs` | Libraries to link: `"m"` becomes `-lm` or `m.lib`; a name with a `.` (`"libfoo.a"`, `"foo.lib"`) is passed as a file. Folders become `-L` or `/link /LIBPATH:`. |
| `archiver` | Archiver override for static libraries, e.g. `"x86_64-w64-mingw32-ar"`; needs a `compiler` family. |
| `msvc_arch` / `msvcArch` | vcvarsall architecture for MSVC discovery: `x64`, `x86`, `arm64`, or a cross pair such as `x64_x86`. Default: the architecture of this ProofRunKit build. |
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
| `output` | Path of the produced file (executables: with `.exe` on Windows). |
| `kind` | The output kind, as requested. |
| `msvc_discovered` / `msvcDiscovered` | `true` when `cl` / `lib` came from vswhere and vcvarsall rather than PATH. |
| `errors`, `warnings` | Counts of parsed errors and warnings. |
| `diagnostics` | Sequence of `{severity, file, line, column, code, message}`; `severity` is `"error"`, `"warning"`, or `"note"`, `code` is the MSVC code (`C2065`) or the GCC/Clang option (`-Wunused-variable`). At most 1000 are kept; the counts include all. |

A missing compiler is returned, not raised: `outcome` is `"failed to start"` and `error` says why (with AUTO:
`no C/C++ compiler found (tried gcc, clang)`; for MSVC: `cl is not on PATH and MSVC discovery failed: ...`), so a test
can `prk.skip_test` on machines without a toolchain.

### Separate compile and link, and libraries

```lua
local cc = "gcc"   -- or "msvc", "clang"; use .obj / .lib names for MSVC
prk.expect_exit_code(prk.compile{compiler = cc, kind = "object", sources = "src/mathlib.c", output = "build/mathlib.o"}, 0)
prk.expect_exit_code(prk.compile{compiler = cc, kind = "static_library", sources = {"build/mathlib.o"},
                                 output = "build/libmathlib.a"}, 0)
local app = prk.compile{compiler = cc, sources = {"src/main.c"}, includes = {"src"}, library_dirs = {"build"},
                        libraries = {"mathlib"}, output = "build/app"}
prk.expect_exit_code(app, 0)
prk.expect_exit_code(prk.run_process{program = app.output}, 0)
```

Object files can also be linked directly by listing them in `sources`.

## Command lines

| Family | Command |
| --- | --- |
| gcc / clang | `gcc -fdiagnostics-color=never -Wall -Wextra [-Werror] [-std=S] -I... -D... sources -o OUT flags...` (`g++` / `clang++` for C++) |
| MSVC | `cl /nologo /W4 [/WX] [/EHsc] [/std:S] /I... /D... sources /FeOUT flags...` |

Object files add `-c` / `/c` and write with `-o` / `/Fo`; shared libraries add `-shared` (and `-fPIC` off Windows) /
`/LD`; libraries follow the sources (`-L... -l...`) or come after `/link` (`/LIBPATH:... name.lib`). Static libraries
run the archiver instead of the compiler: `ar rcs OUT objects...` or `lib /nologo /OUT:OUT objects...`.

AUTO tries, in order, Windows: MSVC (PATH, then discovery), `clang`, `gcc`; elsewhere: `gcc`, `clang`. It moves on
only when a compiler fails to start; a compiler that runs and reports errors is the answer.

## MSVC discovery (vswhere and vcvarsall)

When the MSVC family is chosen (explicitly or by AUTO) and `cl` (or `lib`) is not on PATH, ProofRunKit finds Visual
Studio itself, so tests do not need a Developer Command Prompt:

1. `"%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe" -latest -products * -requires
   Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath -utf8 -nologo`, started directly (the
   folder is read from the environment, default `C:\Program Files (x86)`), gives the newest installation
   (Community, Professional, Enterprise, or Build Tools) with the C++ tools.
2. `cmd.exe /d /u /c call "<installation>\VC\Auxiliary\Build\vcvarsall.bat" <arch> >nul && set` loads the
   environment for `<arch>` and lists it; `/u` makes the listing UTF-16LE, so any text in it survives. `cl.exe` is
   `%VCToolsInstallDir%bin\Host%VSCMD_ARG_HOST_ARCH%\%VSCMD_ARG_TGT_ARCH%\cl.exe` from that environment.
3. `cl.exe` (and `lib.exe` next to it) then run by full path with exactly that environment.

Discovery takes a few seconds, so its result (found or not) is cached for the whole `prk run`: every script and test
shares it. Nothing from a script reaches `cmd.exe` except an architecture from a fixed list; an installation path
containing `"` or `%` is rejected. An explicit `program` / `archiver` is never replaced by discovery. On Linux and macOS
there is no `cmd.exe`, so MSVC is simply "not found".

Requirements: Visual Studio 2017 or later (vswhere ships with the installer since 15.2) or the Build Tools, with the
"Desktop development with C++" workload. On 32-bit Windows `%ProgramFiles(x86)%` does not exist and discovery reports
that vswhere did not run; use a Developer Command Prompt there.

Why it is built this way ([#74](https://github.com/Patrick-W-McMahon/ProofRunKit/issues/74)): an earlier version ran
vswhere through `cmd.exe /c` and chained `chcp`, `echo`, and `where` into the vcvarsall command, and the x86 self-test
binary (which also held test strings such as `x64&calc`) disappeared right after linking, most likely removed by an
antivirus heuristic (not confirmed). The command lines are now as short as possible and the tests use harmless
strings.

## Diagnostics parsing

| Family | Line forms |
| --- | --- |
| GCC, Clang | `file:line:col: error: msg [-Wflag]`, `file:line: warning: msg`, `file:line:col: note: msg`, `cc1: fatal error: msg`, `collect2: error: msg` |
| MSVC | `file(line): error C2065: msg`, `file(line,col): warning C4100: msg`, `file(line): note: msg`, `LINK : fatal error LNK1561: msg`, `cl : command line warning D9002: msg` |

Other lines (source excerpts, "In function" headers, the cl banner) are ignored. Windows paths with drive letters
parse correctly because the location is read backward from the severity token.

## Not covered

- Per-target presets (MinGW cross prefixes, `-m32`): pass `program`, `archiver`, and `flags` instead.
- Build systems (CMake, MSBuild, make): run them with `prk.run_process`.

## Tests

- `proofrunkit_selftest`: `c_driver.*` (parsers for gcc, clang, and MSVC lines; command lines for every kind and the
  library options; output paths; vswhere and vcvarsall parsers; discovery, the PATH-to-discovery fallback, and the cache
  through a fake runner; `prk.compile` in Lua and JavaScript; one real compile that skips without a compiler; and
  `c_driver.discovers_real_msvc`, which on Windows runs the real vswhere/vcvarsall and compiles with the discovered
  `cl`, skipping when Visual Studio's C++ tools are not installed).
- The `prk.tests.json` entries `self.compile` and `js.compile`: compile the samples in
  [`tests/drivers/c`](../tests/drivers/c), run the programs, and check warnings, `werror`, parsed errors, separate
  compile and link, a static library linked through `libraries` / `library_dirs`, and a shared library. Every test
  skips when no compiler is found.
