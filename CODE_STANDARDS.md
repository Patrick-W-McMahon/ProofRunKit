# ProofRunKit code standards

Every commit to ProofRunKit follows these rules. They apply the shared developing-bot coding standards to a C17 /
C++20 / Assembly code base and add the rules specific to this project. Where this document is silent, the shared
standards apply; this document never weakens them.

A change is not done if it breaks either of the two **core rules** below, even when everything else is fine.

## Core rule 1: function names are self-explanatory

- Names are **verb-first** and state the action and its result: `prk_register_test_case`, `prk_read_cycle_counter`,
  `parse_command_line`, `format_summary_as_json`. A reader can call the function correctly from its name and types.
- Boolean functions and variables read as a question: `is_`, `has_`, `should_`, `can_` (`is_valid_test_name`).
- **Forbidden as a name, or as the whole name:** `handle`, `process`, `run` (for a public function), `data`, `tmp`,
  `temp`, `util`, `utils`, `helper`, `helpers`, `fn`, `cb`, `stuff`, `foo`, `go`, `do_it`. Name the outcome instead.
- Abbreviations only when they are well known in the project (`id`, `url`, `cli`, `json`, `asm`, `abi`). Do not invent
  new ones.
- Name length follows scope: a three-line local helper may be short; a public function may not.

## Core rule 2: functions are immutable and testable

- **No argument mutation.** Inputs are `const T*`, `const T&`, or values. A function that must write through a pointer
  is a documented mutator, and its out-parameter is named `out_*`.
- **`const` by default** for locals, parameters, and member functions. Non-`const` needs a reason.
- **No hidden global or static state** in core logic. Lifecycle state (registries, interpreter states) is created at
  the composition root (`main`) and passed in.
- **Side effects live at the edges.** Filesystem, process spawn, environment, clock, and console IO sit in thin
  adapters. Core logic receives what it needs as arguments (for example a reporter callback, a clock function, or an
  output stream) so it can be tested without real IO.
- **Every public function has a test**, landed in the same change as the function: a prk script in `prk.tests.json`
  when scripts or the command line can reach it, otherwise a C++ case in `tests/selftest/` (see Testing requirements).
- **Functions stay small.** A function over 50 lines or a file over 400 lines needs review; over 100 lines or 1,000
  lines it must be split unless the file explains why (vendored code under `third_party/`, generated code, or a data
  table). Nesting is limited to 4 levels and parameters to about 5; past that, pass a struct.

## Languages and toolchains

| Area | Standard |
| --- | --- |
| Core (`src/core`, `src/timing`, `include/`) | C17 (`CMAKE_C_STANDARD 17`, extensions off). Public C ABI only in `include/proofrunkit/`. |
| Front end (`src/cpp`, `src/main.cpp`, tests) | C++20 (`CMAKE_CXX_STANDARD 20`, extensions off). |
| Assembly (`src/asm`) | MASM (`.asm`) on Windows; GAS (`.S`) on Linux/macOS. |
| Build | CMake 3.21+ with `CMakePresets.json`. |

### C17 and C++20 style

- **Naming:** functions and variables `snake_case`; types (`struct`, `enum`, `class`) `PascalCase`; macros and enum
  constants `UPPER_SNAKE_CASE`. Every symbol exported from the C core is prefixed `prk_` (functions) or `Prk` (types),
  and its constants `PRK_`.
- **Headers:** include guards of the form `PROOFRUNKIT_<PATH>_H`; public C headers are wrapped in `extern "C"`.
- **C++:** prefer `std::string_view`, `std::span`, and `std::optional` for inputs and results; use RAII for every
  owned resource; ownership is visible in the signature (`const T&`, `T`, `std::unique_ptr<T>`). No exceptions cross
  the C ABI. `noexcept` on functions that cannot fail.
- **C:** check every allocation; free in reverse order of acquisition; no variable-length arrays.
- **Warnings:** the code builds cleanly with MSVC `/W4` and gcc/clang `-Wall -Wextra -Wpedantic` (plus the extra
  warnings in `cmake/ProofRunKitWarnings.cmake`). Configure with `-DPRK_WARNINGS_AS_ERRORS=ON` before review.

### Formatting

- Formatting is defined by [`.clang-format`](.clang-format) (Google-based, 4-space indent, 120-column limit,
  `PointerAlignment: Left`) and [`.editorconfig`](.editorconfig) (UTF-8, LF line endings, final newline, trimmed
  trailing whitespace). Both come from the shared developing-bot configs; do not add a second formatter.
- Run `clang-format -i` on changed C and C++ files before committing.
- Assembly uses 4-space indentation and the same 120-column limit.

### Documentation

- Every source file starts with a header comment that says what the module is for and what it must not do.
- **Doxygen on every public function and type** (`include/` and every non-`static` function in `src/`): purpose,
  `@param` for each parameter, `@return`, errors or status codes, and side effects (write "none" when the function is
  pure).
- Comments explain **why**, not what: a safety gate, an ABI requirement, a compatibility quirk, or a non-obvious
  invariant. Do not restate the next line.
- Public structs get field-level comments when the field name is not enough.

### Error handling

- The C core reports failure with a `PrkStatus` return value. It never aborts, exits, or prints on behalf of the
  caller. Results are written through `out_*` parameters only when the call succeeds.
- Validate every pointer and size at the public boundary and return `PRK_STATUS_INVALID_ARGUMENT` rather than
  crashing.
- Allocation failure returns `PRK_STATUS_OUT_OF_MEMORY` and leaves the caller's state unchanged.
- C++ code may use exceptions internally but must catch them at the C ABI boundary and at `main`, and map them to
  a status code or a documented process exit code.
- Script errors (Lua, JavaScript) are reported as test failures with the script's message and location; they never
  terminate the kit.

## Assembly conventions

Assembly is used only for measured hot paths. Each routine follows these rules:

- **Mandatory portable C fallback.** Every Assembly routine has a C implementation with the same prototype and
  behavior. CMake (`cmake/ProofRunKitCycleCounter.cmake` and its successors) selects Assembly or the fallback per
  target, and `-DPRK_FORCE_C_FALLBACK=ON` builds every hot path from C. The fallback is always compiled so it can be
  tested.
- **The routine names its fallback.** The file header states the C prototype, the header that declares it, the name
  and file of its C fallback, the ABI, the registers it touches, and the test that covers it.
- **A test compares both.** `tests/selftest/` contains a test that calls the Assembly routine and its C fallback and
  checks that they agree (or, for counters and clocks, that both behave consistently), and asserts which
  implementation was selected.
- **Windows x64 (MASM, `ml64.exe`):** Microsoft x64 calling convention. Arguments in RCX, RDX, R8, R9 (XMM0-XMM3 for
  floating point), return in RAX/XMM0. RBX, RBP, RDI, RSI, RSP, R12-R15, and XMM6-XMM15 are callee-saved. The caller
  reserves 32 bytes of shadow space and the stack is 16-byte aligned at each call. Non-leaf functions need unwind
  data (`PROC FRAME` with `.pushreg` / `.allocstack` / `.endprolog`).
- **Windows x86 (MASM, `ml.exe`):** `.model flat, C` and **cdecl** by default (caller cleans the stack, arguments on
  the stack, return in EAX or EDX:EAX). Use **stdcall** only to match a Windows API and say so in the header. EBX, ESI,
  EDI, and EBP are callee-saved; EAX, ECX, and EDX are volatile. Objects are assembled with **`/safeseh`** so they
  link into images built with `/SAFESEH` (the MSVC 32-bit default).
- **Linux/macOS x86-64 (GAS, `.S`):** System V AMD64 ABI. Arguments in RDI, RSI, RDX, RCX, R8, R9; return in RAX.
  RBX, RBP, R12-R15 are callee-saved. Mark the stack non-executable (`.section .note.GNU-stack`) on ELF.
- **arm64 (GAS, `.S`):** AAPCS64. Arguments in X0-X7, return in X0; X19-X28, FP, and LR are callee-saved, and SP is
  16-byte aligned.
- **No self-modifying code, no runtime code generation**, and no instructions that require elevated privileges. These
  keep the binary stable and avoid antivirus heuristics ([docs/stable-binary.md](docs/stable-binary.md)).
- Comments explain why a sequence is correct for the ABI, not what each instruction does.

## Testing requirements

- **Every public function has a test**, named after the behavior it proves (`registry.rejects_duplicate_names`).
  Prefer a prk script listed in `prk.tests.json`; use a C++ case in `tests/selftest/` only for internals that scripts
  cannot reach (C ABI argument checks, injected clocks, fake runners). [docs/self-hosting.md](docs/self-hosting.md)
  keeps the map.
- Tests are deterministic: no dependence on wall-clock time, test order, network, or the user's environment. Inject
  clocks, file systems, and process runners.
- Bug fixes include a test that fails before the fix and passes after it.
- Every Assembly routine has a test comparing it with its C fallback (see above).
- `prk test --build-dir <build>` (or `ctest`, which runs it) must pass on every architecture a change affects (x64 and, when relevant, x86) before review, and with
  `-DPRK_FORCE_C_FALLBACK=ON` when Assembly or its selection logic changes.
- Tests are never compiled into the shipped `proofrunkit` executable; the self-test is a separate executable
  (`proofrunkit_selftest`) that is not installed and runs through `prk test` as the `cpp.selftest` entry.

## Commits and reviews

- Every change starts with a GitHub issue, and every commit references it with `Refs #N` or `Closes #N`
  ([CONTRIBUTING.md](CONTRIBUTING.md)).
- Behavior-preserving refactors (splitting files, renaming) land in their own commit or pull request with tests green
  before and after.
- PR checklist:
  - [ ] Public function names state the action and the result (core rule 1).
  - [ ] No argument mutation, no new hidden state, side effects at the edges, IO and clock injected (core rule 2).
  - [ ] Each new or changed public function has a unit test (core rule 2).
  - [ ] Formatted with `clang-format`; builds warning-free.
  - [ ] File headers and Doxygen list purpose, parameters, returns, errors, and side effects.
  - [ ] Assembly changes keep a C fallback and a comparison test.
