# Building and testing ProofRunKit on Windows (x64 and x86)

Windows x64 with MSVC is the first supported build and test host (#7, #23). 32-bit (Win32) MSVC
presets and a 32-bit MASM hot path are available for the x86 installer (#38, #39). Linux and macOS
presets exist for future updates but are untested (#34, #35). There are no GitHub Actions
workflows; every build and test runs locally.

## Prerequisites

| Tool | Version | Notes |
| --- | --- | --- |
| Visual Studio | 2026 (v18) with **Desktop development with C++** | Provides `cl.exe`, `link.exe`, `rc.exe`, `ml64.exe` (64-bit MASM), and `ml.exe` (32-bit MASM). |
| CMake | 3.21 or newer (4.x recommended) | Must know the `Visual Studio 18 2026` generator. |
| Git | any recent | |

Ninja is **not** required: the presets use the Visual Studio generator. `ml64.exe` and `ml.exe` ship with the MSVC tools; CMake finds them automatically through the
Visual Studio generator (`x64` or `Win32`).

To check what is installed, run (read-only):

```powershell
& "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe" -latest -products * `
    -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
cmake --version
git --version
```

## Configure, build, and test

From a Developer PowerShell (or any shell, since the Visual Studio generator locates MSVC itself):

```powershell
cmake --preset windows-msvc-x64-debug
cmake --build --preset windows-msvc-x64-debug
ctest --preset windows-msvc-x64-debug --output-on-failure
```

Use `windows-msvc-x64-release` for an optimised, reproducible (`/Brepro`) build. Outputs land in
`build/<preset>/<Debug|Release>/`.

For the 32-bit (Win32) build, enter a 32-bit developer shell and use the x86 presets. On a 64-bit
machine that is `Launch-VsDevShell.ps1 -Arch x86 -HostArch amd64`. The 32-bit hot path is
`src/asm/x86_windows.asm` (MASM, `ml.exe`, **cdecl**; RDTSC returns EDX:EAX). CMake passes `/safeseh`
to `ml.exe`. The self-test asserts the implementation name `asm-masm-x86`. `PRK_FORCE_C_FALLBACK=ON`
still selects the portable C fallback.

```powershell
cmake --preset windows-msvc-x86-debug
cmake --build --preset windows-msvc-x86-debug
ctest --preset windows-msvc-x86-debug --output-on-failure
```

Or run `.\installeruild-x86.ps1`, which aborts if the working tree is dirty and never stashes,
resets, or discards changes.

## What CTest runs

| Test | Checks |
| --- | --- |
| `proofrunkit_selftest` | Registry, runner, reporters, CLI parsing, and the MASM cycle counter vs. its C fallback. |
| `proofrunkit_version` | `proofrunkit --version` prints the version and `asm-masm-x64`. |
| `proofrunkit_builtin_checks` | `proofrunkit` with no scripts runs its built-in environment checks. |
| `proofrunkit_rejects_unknown_option` | Bad options produce a usage error. |

Run the self-test directly for per-test output: `build\windows-msvc-x64-debug\tests\selftest\Debug\proofrunkit_selftest.exe`
(it accepts `--list`, `--filter TEXT`, and `--format json`).

## Options

| CMake option | Default | Effect |
| --- | --- | --- |
| `PRK_FORCE_C_FALLBACK` | `OFF` | Build every hot path from its portable C fallback instead of Assembly. |
| `PRK_WARNINGS_AS_ERRORS` | `OFF` | Add `/WX` (MSVC) or `-Werror`. |
| `PRK_REPRODUCIBLE_BUILD` | `ON` | MSVC `/Brepro` so identical sources produce identical binaries. |
| `PRK_BUILD_SELFTEST` | `ON` | Build `proofrunkit_selftest` and register CTest tests. |

Example: `cmake --preset windows-msvc-x64-debug -DPRK_FORCE_C_FALLBACK=ON`.

## Recording results

After a run on the Windows test host, record the OS build, Visual Studio and CMake versions, and the
CTest pass/fail counts on #23 (see its acceptance criteria).

## Packaging the x64 MSI (#37)

The `windows-msvc-x64-release` preset feeds the WiX v4 installer. Prerequisites
(the .NET SDK and WiX 4.0.6), silent-install flags, the UpgradeCode, and a
read-only verification script are documented in [installer-windows-x64.md](installer-windows-x64.md).

```powershell
.\installer\build-msi-x64.ps1
.\installer\verify-msi-x64.ps1
```

The 32-bit MSI (#38) is documented in [installer-windows-x86.md](installer-windows-x86.md):

```powershell
.\installer\build-x86.ps1
.\installer\build-msi-x86.ps1
.\installer\verify-msi-x86.ps1
```
