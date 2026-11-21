<p align="center">
  <img src="docs/assets/banner.svg" alt="ProofRunKit — a single high-performance test executable, compiled once" width="100%">
</p>

<p align="center">
  <img src="docs/assets/badge-license.svg" alt="License: Apache-2.0">
  <img src="docs/assets/badge-windows.svg" alt="Platform: Windows">
  <img src="docs/assets/badge-status.svg" alt="Status: planning / pre-alpha">
</p>

# ProofRunKit

**Status: planning / pre-alpha.** This repository is in design. The executable, embedded interpreters, drivers, and installers described here are planned work, not shipped features. Follow progress in the [open issues](https://github.com/Patrick-W-McMahon/ProofRunKit/issues).

ProofRunKit is a single high-performance test executable, written in C and C++ with Assembly on hot paths, that loads and runs tests from the outside. It is compiled once and then reused. Tests change; the binary does not.

## Why ProofRunKit exists

Today, several of Patrick W. McMahon's projects ship a custom C or C++ test executable that is rebuilt whenever the tests change. Each rebuild is a new file. Norton antivirus treats that new file as an unknown binary and flags it again.

ProofRunKit is meant to replace those per-project test executables with one stable program:

- The kit is compiled once and stays a stable binary.
- Tests live on disk as scripts or as inputs to real compilers and runtimes. They are never compiled into ProofRunKit itself.
- When tests change, you change the tests, not the executable, so Norton is not asked to re-trust a brand-new test binary on every rebuild.

Windows is the first supported test host. That choice is deliberate: the Norton rebuild problem shows up there first.

## How tests are written

Tests for the kit itself are written in **Lua** or **JavaScript**. The compiled executable will load them through embedded interpreters (Lua 5.4, and a JavaScript engine still to be selected) and run them without a rebuild.

Existing project suites that already use Node.js, Vitest, pytest, doctest, or other runners are a later, gated migration. Those suites are not rewritten in this first slice; the kit will grow drivers that can wrap them.


<p align="center">
  <img src="docs/assets/diagram-test-flow.svg" alt="Planned flow: agents write Lua or JavaScript tests, the single proofrunkit.exe loads and runs them, and results are reported. The executable does not change when tests change." width="100%">
</p>

## Languages under test

The planned surface covers:

| Language | Planned approach |
| --- | --- |
| C and C++ | Shell out to gcc, clang, or MSVC `cl`; capture diagnostics and artifacts; run the produced binary |
| C# | Shell out to `dotnet`; capture diagnostics; run the produced program |
| Assembly | Assemble with `ml64` on Windows, or GAS / NASM elsewhere; link and run where applicable |
| JavaScript and TypeScript | Embedded engine for kit-authored tests; Node.js, `node:test`, and Vitest for existing suites |
| Python | System Python and pytest |
| HTML and CSS | Validation and structure checks; Chrome-driven checks in a later ticket |

None of these drivers are implemented yet.

## Hybrid execution model

ProofRunKit is not an in-process compiler and not a replacement for gcc, MSVC, Node, or Python. It is a stable runner that shells out to tools you already have:

- For compiled languages, the kit will invoke the real toolchain, capture warnings, errors, exit codes, and output files, then launch the produced binary to check behavior (exit code, stdout, stderr, timing).
- For script suites, it will invoke `node` or `python` the same way, including wrappers for `node:test`, Vitest, and pytest.
- HTML and CSS follow a check path (static first; browser-driven later) rather than a compile path.


<p align="center">
  <img src="docs/assets/diagram-hybrid-execution.svg" alt="Planned hybrid execution: compiled languages shell out to gcc, clang, MSVC cl, dotnet, or assemblers; diagnostics, exit codes, and outputs are captured, then produced binaries are run. Script suites run on node or python." width="100%">
</p>

## Architecture

The planned executable is one process with clear seams:

- A public C ABI and C/C++ core (CLI, test registry and runner, process spawn, fixtures, timing).
- Embedded Lua 5.4 and an embedded JavaScript engine, so kit-authored tests load at runtime.
- Assembly hot paths (for example `prk_read_cycle_counter`) with a **mandatory portable C fallback** selected by CMake.
- Reporters for the console, JSON, and JUnit XML, with a documented exit-code contract.

User tests stay outside that binary. Architecture details, including the JavaScript engine recommendation, will live in `docs/ARCHITECTURE.md` and `docs/PROJECT_BRIEF.md` (see [issue #24](https://github.com/Patrick-W-McMahon/ProofRunKit/issues/24)). The engine choice needs Grok Bot sign-off before it is finalized ([issue #10](https://github.com/Patrick-W-McMahon/ProofRunKit/issues/10)).


<p align="center">
  <img src="docs/assets/diagram-architecture.svg" alt="Planned compiled core: CLI, test registry and runner, reporters, embedded Lua 5.4 and a JavaScript engine, a C/C++ core, and Assembly hot paths with a portable C fallback selected by CMake." width="100%">
</p>

## Platform status

**Windows is tested first.** Linux and macOS builds and testing will come in future updates.

CMake presets for Linux and macOS may appear earlier than official testing on those hosts. A preset that exists is not the same thing as a supported, tested platform. See [issue #23](https://github.com/Patrick-W-McMahon/ProofRunKit/issues/23) for the Windows build and self-test, and [issue #34](https://github.com/Patrick-W-McMahon/ProofRunKit/issues/34) and [issue #35](https://github.com/Patrick-W-McMahon/ProofRunKit/issues/35) for Linux and macOS testing.

## Installers

Windows installers for **x64** and **x86** are planned as a per-machine install of the stable executable (Program Files on 64-bit, Program Files (x86) for the 32-bit build on 64-bit Windows). Installer technology, 32-bit presets, and a code-signing decision (needed so Norton and SmartScreen are less likely to flag an unsigned installer) are tracked under [issue #36](https://github.com/Patrick-W-McMahon/ProofRunKit/issues/36). Spending on a certificate or Azure Artifact Signing needs Patrick's explicit OK ([issue #41](https://github.com/Patrick-W-McMahon/ProofRunKit/issues/41)).

## Roadmap

Work is ticket-first. The project is **not done** until ProofRunKit has replaced at least one real existing project test suite end to end ([issue #26](https://github.com/Patrick-W-McMahon/ProofRunKit/issues/26)). The proposed first candidate is Trading Agent's doctest suite ([issue #28](https://github.com/Patrick-W-McMahon/ProofRunKit/issues/28)). SkyNet's suite is excluded as the first candidate while SkyNet is paused ([issue #33](https://github.com/Patrick-W-McMahon/ProofRunKit/issues/33)).

Migrations proceed **one project at a time**, in sequence, with four gated steps: create ProofRunKit configs; verify pass/fail case-for-case against the old suite; confirm the new system fully replaces the old solution; only then consider removing old test tools, and only with Patrick's explicit OK. Never skip verification. **Old tools stay as fallback** until that OK ([issue #27](https://github.com/Patrick-W-McMahon/ProofRunKit/issues/27)). Changes inside another project's repository go through that project's own developing bot.

Planned suite migrations (after the first Windows-capable core exists):

- [Trading Agent, C++ / doctest (~60 cases)](https://github.com/Patrick-W-McMahon/ProofRunKit/issues/28) — first in line
- [Jarvis, TypeScript / Vitest (~105 cases)](https://github.com/Patrick-W-McMahon/ProofRunKit/issues/29)
- [Media Studio, JavaScript / Vitest (~310 cases)](https://github.com/Patrick-W-McMahon/ProofRunKit/issues/30)
- [Political Website, node:test (~29 cases)](https://github.com/Patrick-W-McMahon/ProofRunKit/issues/31)
- [AI Designer Tools, pytest (~78 cases)](https://github.com/Patrick-W-McMahon/ProofRunKit/issues/32)
- [SkyNet, C++ header-only harness (~105 cases)](https://github.com/Patrick-W-McMahon/ProofRunKit/issues/33) — blocked while SkyNet is paused

## Contributing

Contribution guidelines will live in [`CONTRIBUTING.md`](CONTRIBUTING.md). That file is not in the tree yet; it is tracked as [issue #5](https://github.com/Patrick-W-McMahon/ProofRunKit/issues/5). Until then: work is issue-first, every commit should reference its issue (`Refs #N` or `Closes #N`), and there is no GitHub Actions CI while the Actions budget is exhausted. Build and test locally. Windows is the first test host.

## License

ProofRunKit will be licensed under the **Apache License 2.0**. The `LICENSE` file (verbatim Apache-2.0 text, so GitHub can detect it) and a `NOTICE` file will land with [issue #3](https://github.com/Patrick-W-McMahon/ProofRunKit/issues/3). They are not in the repository yet.

## Disclaimer

A separate liability notice will live in [`DISCLAIMER.md`](DISCLAIMER.md) and will be linked here when it lands. That file is tracked as [issue #4](https://github.com/Patrick-W-McMahon/ProofRunKit/issues/4). It will not modify the Apache License 2.0 terms.

## Attribution

If you use ProofRunKit, please credit the project.

Copyright 2026 Patrick W. McMahon. Repository: [https://github.com/Patrick-W-McMahon/ProofRunKit](https://github.com/Patrick-W-McMahon/ProofRunKit).

Downstream redistributions should retain attribution notices. See `NOTICE` (to be added in [issue #3](https://github.com/Patrick-W-McMahon/ProofRunKit/issues/3)) per Apache License 2.0, section 4(d).
