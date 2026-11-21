# Contributing to ProofRunKit

Thank you for helping improve ProofRunKit. This guide explains how work is proposed, built, tested, reviewed, and
merged. Please read it before opening a pull request.

## Ground rules

1. **Issue first: no code change without a ticket.** Every bug fix, feature, refactor, or documentation change
   starts with a GitHub issue. Search the [issue tracker](https://github.com/Patrick-W-McMahon/ProofRunKit/issues)
   first; if nothing fits, open a new issue describing the problem, the proposed change, and how it will be verified.
   Wait for the maintainer to acknowledge larger changes before investing in them.
2. **Every commit references an issue.** Each commit message includes `Refs #N` or `Closes #N` (see
   [Commit messages](#commit-messages)). Commits without an issue reference are not merged.
3. **Follow the code standards.** All C, C++, and Assembly follows [CODE_STANDARDS.md](CODE_STANDARDS.md).
4. **Build and test locally.** There is no CI (see [No CI](#no-ci-build-and-test-locally)). A pull request is ready
   for review only when it builds and its self-test passes on your machine.

## Workflow

1. **Fork** the repository and clone your fork.
2. **Create a branch** from an up-to-date `main`, named `<type>/<short-description>`:

   | Prefix | Use for | Example |
   | --- | --- | --- |
   | `feat/` | New features | `feat/lua-runner` |
   | `fix/` | Bug fixes | `fix/msi-path-check` |
   | `docs/` | Documentation only | `docs/contributing` |
   | `refactor/` | Behavior-preserving restructuring | `refactor/split-reporters` |
   | `test/` | Tests only | `test/registry-edge-cases` |
   | `build/` | CMake, presets, installer, and scripts | `build/x86-presets` |

3. **Make focused commits**, each referencing its issue.
4. **Build and run the self-test** (below) for every architecture your change affects.
5. **Open a pull request** against `main`. In the description, link the issue with `Closes #N` when the PR fully
   resolves it, or `Refs #N` when it does not (and comment on the issue describing what remains). Include the
   commands you ran and their results (OS, toolchain versions, and pass/fail counts).

## Commit messages

```text
Short imperative summary (72 characters or fewer)

Why the change is needed and what it does, wrapped at 72 columns.
Mention anything a reviewer should check.

Refs #12
```

- Use `Closes #N` (or `Fixes #N`) in the commit or PR that fully resolves an issue; GitHub closes the issue when the
  PR merges into `main`.
- Use `Refs #N` for partial work, and leave a comment on the issue saying what was done and what remains.
- A commit may reference more than one issue (`Refs #7, Refs #23`).
- Bugs or follow-ups you discover while working get their own issue before you fix them. Do not fold an
  untracked fix into an unrelated commit.

## Building and testing on Windows

Windows is the supported build and test host today. Linux and macOS presets exist but are untested; they will be
enabled in future updates ([#34](https://github.com/Patrick-W-McMahon/ProofRunKit/issues/34),
[#35](https://github.com/Patrick-W-McMahon/ProofRunKit/issues/35)).

Prerequisites, presets, options, and installer packaging are documented in
[docs/building-windows.md](docs/building-windows.md). The short version, from a Developer PowerShell for Visual
Studio 2026:

```powershell
# 64-bit
cmake --preset windows-msvc-x64-debug
cmake --build --preset windows-msvc-x64-debug
build\windows-msvc-x64-debug\Debug\proofrunkit.exe test --build-dir build\windows-msvc-x64-debug

# 32-bit (from an x86 developer shell: Launch-VsDevShell.ps1 -Arch x86 -HostArch amd64)
cmake --preset windows-msvc-x86-debug
cmake --build --preset windows-msvc-x86-debug
build\windows-msvc-x86-debug\Debug\proofrunkit.exe test --build-dir build\windows-msvc-x86-debug
```

`prk test` is the way to test ProofRunKit: it runs every entry of [`prk.tests.json`](prk.tests.json) (smoke tests,
Lua and JavaScript scripts, samples, and the internal C++ cases in `proofrunkit_selftest` as the `cpp.selftest`
entry) with one combined summary ([docs/test-config.md](docs/test-config.md),
[docs/self-hosting.md](docs/self-hosting.md)). `ctest --preset <preset> --output-on-failure` runs the same suite once
through its `prk_test_suite` entry, plus a few build checks.

Before requesting review:

- [ ] The build has no warnings (`-DPRK_WARNINGS_AS_ERRORS=ON` is recommended).
- [ ] `prk test --build-dir <build>` passes (or `ctest`, which runs it as `prk_test_suite`).
- [ ] If you touched Assembly, the build also passes with `-DPRK_FORCE_C_FALLBACK=ON`.
- [ ] If you touched the installer, the MSI builds and its `installer\verify-msi-*.ps1` check passes
      ([docs/installer-windows-x64.md](docs/installer-windows-x64.md),
      [docs/installer-windows-x86.md](docs/installer-windows-x86.md)).
- [ ] New or changed public functions have tests: a prk script listed in `prk.tests.json` when the behavior is
      reachable from a script or the command line, otherwise a C++ case in `tests/selftest/`.

## No CI: build and test locally

The repository currently has **no continuous integration**. The GitHub Actions budget is paused, so:

- **Do not add, enable, or modify GitHub Actions workflows** (`.github/workflows/`). Pull requests that add workflows
  will not be merged while Actions is paused.
- Run the build and self-test locally and paste the results into the pull request.

## Review expectations

- The maintainer reviews every pull request. Expect questions about naming, testability, and whether the change keeps
  the executable stable (tests must never be compiled into the binary; see
  [docs/stable-binary.md](docs/stable-binary.md)).
- Keep pull requests small and single-purpose. Behavior-preserving refactors land separately from feature work.
- Address review comments with new commits (each still referencing the issue) rather than force-pushing over
  reviewed history.
- Pull requests are merged with a merge commit into `main`.

## Licensing of contributions

ProofRunKit is licensed under the [Apache License, Version 2.0](LICENSE). Contributions are accepted under the same
license (inbound = outbound, per section 5 of the license): by submitting a pull request you agree that your
contribution is licensed under Apache-2.0. No separate contributor license agreement and no DCO sign-off are
required. Do not submit code you do not have the right to license this way.

Third-party code (for example vendored interpreters under `third_party/`) must have a license compatible with
Apache-2.0, keep its original license file, and be recorded in [NOTICE](NOTICE).

See also [DISCLAIMER.md](DISCLAIMER.md).

## Security and secrets

Never commit credentials, tokens, keys, or machine-specific secrets. Report a security problem by opening an issue
without exploit details and asking the maintainer for a private channel.
