# ProofRunKit project brief

This brief records why ProofRunKit exists, what it must do, and the permanent rules that govern how it replaces
existing test tools. Keep it accurate as tickets land.

## Why

Projects that rebuild a custom C or C++ test executable whenever their tests change produce a new, unknown binary on
every build. Antivirus products (Norton in particular) flag each rebuilt executable again, which interrupts testing.

ProofRunKit is a **single, high-performance test executable that is compiled once** and then reused. Tests live
outside the executable and are never compiled into it. When tests change, only the tests change; the binary stays the
same file ([#22](https://github.com/Patrick-W-McMahon/ProofRunKit/issues/22),
[stable-binary.md](stable-binary.md)).

## How tests are written

Kit-authored tests are **Lua** or **JavaScript** files on disk. The `proofrunkit` executable loads them at run time
through embedded interpreters:

- **Lua 5.4** ([#9](https://github.com/Patrick-W-McMahon/ProofRunKit/issues/9))
- **QuickJS-ng** ([#10](https://github.com/Patrick-W-McMahon/ProofRunKit/issues/10),
  [#11](https://github.com/Patrick-W-McMahon/ProofRunKit/issues/11))

Both languages share one test-authoring API: registration, assertions, process spawn, file checks, timing, and
fixtures ([#12](https://github.com/Patrick-W-McMahon/ProofRunKit/issues/12)).

## Languages under test

C, C++, C#, Java, Rust, Go, TypeScript, JavaScript, Python, HTML, CSS, and Assembly.

## Hybrid execution model

ProofRunKit is not an in-process compiler. It drives real toolchains and runtimes:

| Kind | What ProofRunKit does |
| --- | --- |
| Compiled languages (C, C++, C#, Java, Rust, Go) | Shell out to `gcc`, `clang`, `cl`, `dotnet`, `javac`, `rustc` / `cargo`, or `go`; capture diagnostics, exit codes, and produced objects; then run the produced binaries and check their behavior. |
| Assembly | Assemble with `ml64` / `ml` (Windows), GAS, or NASM; link; run; check exit code and output. |
| JavaScript and TypeScript | Run on Node.js (and `tsc` for type checks); wrap `node:test` and Vitest. |
| Python | Run on the system Python; wrap pytest. |
| HTML and CSS | Validation and structure checks, then browser-driven checks via Chrome. |

## Test kinds to wrap eventually

ProofRunKit should eventually replace the custom test executables and existing suites in other projects. Those suites
include, among others:

| Kind | Existing suite |
| --- | --- |
| C++ (doctest) | on the order of 60 cases in a custom test executable |
| TypeScript (Vitest) | on the order of 100 tests |
| JavaScript (Vitest) | on the order of 300 tests |
| JavaScript (`node:test`) | on the order of 30 tests |
| Python (pytest) | on the order of 80 tests |
| C++ (in-house header-only harness) | on the order of 100 cases (one such project is paused) |

## Constraints

- **No CI for now.** The GitHub Actions budget is paused; do not add workflows. Build and test locally.
- **Windows first.** Windows x64 and x86 are the tested hosts. Linux and macOS come in future updates
  ([#34](https://github.com/Patrick-W-McMahon/ProofRunKit/issues/34),
  [#35](https://github.com/Patrick-W-McMahon/ProofRunKit/issues/35)).
- **Distribution:** per-machine WiX v4 MSI installers. x64 installs to `%ProgramFiles%\ProofRunKit`; x86 installs to
  `%ProgramFiles(x86)%\ProofRunKit`. Each ships one executable, `proofrunkit.exe`, plus a `prk.cmd` shim so the
  `prk` command works in cmd and PowerShell ([#93](https://github.com/Patrick-W-McMahon/ProofRunKit/issues/93)). Installers are unsigned for now
  ([#36](https://github.com/Patrick-W-McMahon/ProofRunKit/issues/36)-[#41](https://github.com/Patrick-W-McMahon/ProofRunKit/issues/41)).
- **License:** Apache License 2.0 ([LICENSE](../LICENSE), [NOTICE](../NOTICE), [DISCLAIMER.md](../DISCLAIMER.md)).
- **Migration is gated future work** (below).

## Permanent rules

### Acceptance goal

ProofRunKit is **not done** until it has replaced at least one real, existing project test suite end to end, proving
it can fully substitute that project's old custom test tool
([#26](https://github.com/Patrick-W-McMahon/ProofRunKit/issues/26)). The first candidate is a doctest C++ suite in
another project ([#28](https://github.com/Patrick-W-McMahon/ProofRunKit/issues/28)). A paused project's header-only
harness is excluded as the first candidate. Changes inside another project's repository go through that project's own
maintainers.

### Fallback safeguard

Never remove or delete any existing test tool, test executable, or test suite in any project until ProofRunKit has
been confirmed, with the maintainer's explicit OK, to replace it entirely. Old tools stay in place as the fallback
([#27](https://github.com/Patrick-W-McMahon/ProofRunKit/issues/27)).

### Migration protocol

Projects migrate **one at a time**. Each migration has four gated steps, in order:

1. **Create** ProofRunKit test configs for the project.
2. **Verify** they are fully operational and correct, matching the old suite's pass/fail case for case.
3. **Confirm** the new system fully replaces the old solution.
4. **Remove** the old test tools, only with the maintainer's explicit OK for the deletion.

Never skip verification, and never remove old tools before the new ones are proven. Migration tickets are sequential:
each depends on the previous project's steps 1-3
([#28](https://github.com/Patrick-W-McMahon/ProofRunKit/issues/28)-[#33](https://github.com/Patrick-W-McMahon/ProofRunKit/issues/33)).
A paused project is blocked and does not hold up the rest of the queue.

ProofRunKit applies the same rule to itself: once script tests work, it tests itself with its own platform, and its
C++ self-test stays as the fallback until its replacement is approved
([#55](https://github.com/Patrick-W-McMahon/ProofRunKit/issues/55)).
