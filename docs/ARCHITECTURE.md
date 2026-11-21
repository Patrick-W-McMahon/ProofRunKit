# ProofRunKit architecture

This document describes how ProofRunKit is built. Sections marked **Planned** describe designed but not yet
implemented parts; keep them in step with the tickets as they land.

## Overview

```text
                   +-------------------------------------------------------------+
  test.lua  ---->  |  proofrunkit.exe  (compiled once, never contains user tests) |
  test.js   ---->  |                                                             |
                   |  CLI front end (C++20)   ->  reporters (text, JSON, JUnit)  |
                   |        |                                                    |
                   |  script layer: Lua 5.4, QuickJS-ng (sandboxed)              |
                   |        |                                                    |
                   |  C17 core: registry, runner, summary  <- Assembly hot paths |
                   |        |                                   (+ C fallback)   |
                   |  drivers: process spawn -> compilers, runtimes, assemblers, |
                   |           HTML/CSS checks                                   |
                   +-------------------------------------------------------------+
```

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

### Script test layer - Lua done, JavaScript planned

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

### Drivers - Planned

Language drivers sit on a process-spawn and sandbox layer
([#13](https://github.com/Patrick-W-McMahon/ProofRunKit/issues/13)) and invoke real toolchains: C and C++
([#14](https://github.com/Patrick-W-McMahon/ProofRunKit/issues/14)), C#
([#15](https://github.com/Patrick-W-McMahon/ProofRunKit/issues/15)), Assembly
([#16](https://github.com/Patrick-W-McMahon/ProofRunKit/issues/16)), Java
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
