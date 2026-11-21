# ProofRunKit architecture

This document describes how ProofRunKit is built. Sections marked **Planned** describe designed but not yet
implemented parts; keep them in step with the tickets as they land.

## Overview

<p align="center">
  <img src="assets/architecture-overview.svg" alt="ProofRunKit architecture overview: Lua and JavaScript test scripts go into the single compiled proofrunkit.exe (run as prk), which loads them into sandboxed Lua 5.4 or QuickJS-ng engines, registers tests through the prk API into the C17 test plan and runner (with Assembly hot paths and a C fallback), shells out to MSVC, gcc, clang, MASM, GAS, or NASM through the process-spawn layer, and reports console, JSON, or JUnit XML results with an exit code. CMake presets build it, WiX packages the Windows x64 MSI, GitHub Releases publish it with a SHA-256 file, and prk -update installs new versions." width="100%">
</p>

1. **Test scripts.** Tests are `.lua` (Lua 5.4.9) or `.js` / `.mjs` (QuickJS-ng 0.18.0) files. They are loaded at run
   time and never compiled into the executable.
2. **`prk` CLI** (`src/cpp/`). On Windows, `prk` is the `prk.cmd` shim that starts `proofrunkit.exe`. The CLI
   handles `run`, `list`, `-version` / `-v`, and `-update`, plus the selection, timeout, and reporter options.
3. **Script loading.** Picks the engine by file extension and gives each script its own engine state. A script that
   cannot be read or loaded stops the run with exit code 4.
4. **`prk` API.** Sandboxed scripts register tests and reach assertions, suites and hooks, tags, timing, read-only
   files, and processes only through this API.
5. **Test plan and runner** (`src/core/`, `src/script/`). Selects and orders tests, runs hooks, enforces timeouts, and
   builds the summary. Timing uses the Assembly hot path (MASM or GAS) or its portable C fallback, chosen by CMake.
6. **Toolchain shell-out.** `prk.run_process`, `prk.compile`, and `prk.assemble` start real tools through the
   process-spawn layer: MSVC `cl` (found with vswhere and vcvarsall, cached per run), gcc, clang, `ml64` / `ml`, GAS,
   or NASM. Then they compile, link, and run the artifact.
7. **Results back to the test.** Each call returns `ok`, `outcome`, `exit_code`, captured output, and parsed
   diagnostics, and the test asserts on them.
8. **Reporters.** Format the run as console text, JSON, or JUnit XML.
9. **Reports and exit code.** One format goes to stdout, and JSON or JUnit files can be written in the same run. The
   exit code follows [exit-codes.md](exit-codes.md).
10. **CMake presets.** These build a deterministic release executable on Windows (Visual Studio 18 2026), Linux, and
    macOS.
11. **WiX v4 MSI.** Installs the one executable, `proofrunkit.exe`, and the `prk.cmd` shim that makes `prk` work in
    cmd and PowerShell to `%ProgramFiles%\ProofRunKit`, with an optional PATH entry
    ([#93](https://github.com/Patrick-W-McMahon/ProofRunKit/issues/93)).
12. **GitHub Releases.** Publish `ProofRunKit-<ver>-win-x64.msi` with its `.sha256` file.
13. **`prk -update`.** Downloads the latest MSI over WinHTTP, verifies its SHA-256, and runs `msiexec /passive`.
    `prk -v` reports whether you are up to date.

## Layers

### Stable core (C17) - Done

`src/core/` and `include/proofrunkit/prk.h` provide the public C ABI: a test registry, a runner that calls test
bodies with a per-test context, and a summary. Results reach the caller only through reporter callbacks, so the core
does no console or file IO ([#7](https://github.com/Patrick-W-McMahon/ProofRunKit/issues/7)).

### Assembly hot paths - Done

`src/asm/` holds measured hot paths (currently the cycle counter used for timing) in MASM for Windows x64 and x86 and
GAS for Linux/macOS x86-64 and arm64. Every routine has a portable C fallback; CMake selects one per target and
`PRK_FORCE_C_FALLBACK=ON` forces C everywhere. The self-test compares each routine with its fallback
([#8](https://github.com/Patrick-W-McMahon/ProofRunKit/issues/8),
[#39](https://github.com/Patrick-W-McMahon/ProofRunKit/issues/39)).

### Front end (C++20) - Done

`src/cpp/` parses the command line, runs built-in environment checks, and formats results as text or JSON
([#21](https://github.com/Patrick-W-McMahon/ProofRunKit/issues/21) extends this to JUnit XML and the exit-code
contract).

### Script test layer - Lua and JavaScript done

Embedded interpreters load test scripts at run time and register their tests with the core through the same C ABI
([#9](https://github.com/Patrick-W-McMahon/ProofRunKit/issues/9),
[#11](https://github.com/Patrick-W-McMahon/ProofRunKit/issues/11),
[#12](https://github.com/Patrick-W-McMahon/ProofRunKit/issues/12)). Interpreters are vendored as source under
`third_party/`, pinned to a version, recorded in [NOTICE](../NOTICE), and linked statically into the single
executable. Scripts run sandboxed: a memory-limited allocator, an instruction budget, and no ambient filesystem,
process, or module-loading access. Capabilities such as process spawn and file checks are granted only through the
test-authoring API.

Lua 5.4.9 is in place: `src/script/` implements the sandbox and the `prk` table over the C ABI in
`include/proofrunkit/prk_lua.h`, and `src/cpp/script_loading.cpp` reads script files and loads each into its own Lua
state. The test-authoring API (#12) is split so JavaScript can reuse it: `src/script/prk_test_plan.c` owns suites,
hooks, tag and name selection, and the per-test execution order behind an engine callback table, and
`include/proofrunkit/prk_script.h` defines the host callbacks (print, monotonic clock, read-only relative file access)
that `src/cpp/script_host.cpp` implements. See [lua-tests.md](lua-tests.md).

QuickJS-ng 0.18.0 is in place on the same plan and host (#11): `src/script/prk_js_script.c` creates one runtime per
script with a memory limit, a 512 KiB stack limit, and an interrupt handler that enforces the instruction budget and
timeouts, and `src/script/prk_js_api.c` defines the camelCase `prk` object. The C ABI is
`include/proofrunkit/prk_js.h`; `quickjs-libc` (the `std` / `os` modules) is not vendored. See
[js-tests.md](js-tests.md).

### Drivers - Planned

Language drivers sit on the process-spawn layer
([#13](https://github.com/Patrick-W-McMahon/ProofRunKit/issues/13), done: `include/proofrunkit/prk_process.h`,
`src/process/`, an injectable `PrkProcessRunner`, mandatory timeouts that kill the whole process tree; see
[process-spawn.md](process-spawn.md)) and invoke real toolchains: C and C++
([#14](https://github.com/Patrick-W-McMahon/ProofRunKit/issues/14), done: `include/proofrunkit/prk_c_driver.h` and
`prk_msvc_toolchain.h`, `src/drivers/`, `prk.compile`, MSVC found with vswhere/vcvarsall and cached per run; see
[c-cpp-driver.md](c-cpp-driver.md)), C#
([#15](https://github.com/Patrick-W-McMahon/ProofRunKit/issues/15)), Assembly
([#16](https://github.com/Patrick-W-McMahon/ProofRunKit/issues/16), done: `include/proofrunkit/prk_asm_driver.h`,
`src/drivers/prk_asm_driver.c`, `prk.assemble` with MASM, GAS, or NASM, sharing the MSVC discovery cache; see
[asm-driver.md](asm-driver.md)), Java
([#44](https://github.com/Patrick-W-McMahon/ProofRunKit/issues/44)), Rust
([#45](https://github.com/Patrick-W-McMahon/ProofRunKit/issues/45)), Go
([#46](https://github.com/Patrick-W-McMahon/ProofRunKit/issues/46)), Python
([#17](https://github.com/Patrick-W-McMahon/ProofRunKit/issues/17)), Node.js
([#18](https://github.com/Patrick-W-McMahon/ProofRunKit/issues/18)), TypeScript
([#47](https://github.com/Patrick-W-McMahon/ProofRunKit/issues/47)), and HTML/CSS
([#19](https://github.com/Patrick-W-McMahon/ProofRunKit/issues/19),
[#20](https://github.com/Patrick-W-McMahon/ProofRunKit/issues/20)). Each driver captures diagnostics, exit codes, and
outputs, then runs the produced artifact.

## Interpreter decision

| Language | Engine | Status |
| --- | --- | --- |
| Lua | Lua 5.4 (MIT) | Approved; embedding tracked in [#9](https://github.com/Patrick-W-McMahon/ProofRunKit/issues/9). |
| JavaScript | QuickJS-ng (MIT) | Approved by the architecture reviewer; research record in [#10](https://github.com/Patrick-W-McMahon/ProofRunKit/issues/10), embedding in [#11](https://github.com/Patrick-W-McMahon/ProofRunKit/issues/11). |

Both are small, embeddable C libraries with MIT licenses that are compatible with Apache-2.0. QuickJS-ng is preferred
over the original QuickJS for its active maintenance and MSVC support, and over Duktape for modern ECMAScript
coverage.

## Stable binary and antivirus considerations

The design goal is that antivirus products see one known file, not a new one per test change
([#22](https://github.com/Patrick-W-McMahon/ProofRunKit/issues/22), [stable-binary.md](stable-binary.md)):

- Tests are never compiled into the binary; they are loaded as scripts or passed to external toolchains.
- Builds are deterministic (`/Brepro` on MSVC) so identical sources produce identical bytes.
- No self-modifying code, runtime code generation, packers, or self-extraction.
- A VERSIONINFO resource and an `asInvoker` manifest are embedded.
- Compiler outputs will go to a fixed artifact directory once the drivers exist.
- Code signing is a future option, not currently in use
  ([#41](https://github.com/Patrick-W-McMahon/ProofRunKit/issues/41)).

## Distribution

WiX v4 MSI packages per architecture: x64 to `%ProgramFiles%\ProofRunKit` and x86 to `%ProgramFiles(x86)%\ProofRunKit`,
each with an optional system PATH entry and an Add/Remove Programs entry
([docs/installer-windows-x64.md](installer-windows-x64.md), [docs/installer-windows-x86.md](installer-windows-x86.md)).

`prk -update` ([#75](https://github.com/Patrick-W-McMahon/ProofRunKit/issues/75), [update.md](update.md)) reads the
latest GitHub release with WinHTTP, downloads the x64 MSI and its `.sha256`, verifies the SHA-256, and starts
`msiexec /passive` so `MajorUpgrade` replaces the installed version; `prk -version` reports whether a newer release
exists. The network and installer code sit in `prk_update_system`, linked only into the shipped executable; the
command logic in `src/cpp/update/` is driven through injected `UpdateServices` and unit-tested with fakes.
