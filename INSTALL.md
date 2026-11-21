<p align="center">
  <img src="docs/assets/banner.svg" alt="ProofRunKit — a single high-performance test executable, compiled once" width="100%">
</p>

<p align="center">
  <a href="https://github.com/Patrick-W-McMahon/ProofRunKit/releases/latest"><img src="docs/assets/badge-install.svg" alt="Install: Windows x64 MSI"></a>
  <img src="docs/assets/badge-license.svg" alt="License: Apache-2.0">
  <img src="docs/assets/badge-status.svg" alt="Status: planning / pre-alpha">
</p>

# Install ProofRunKit

ProofRunKit ships as one installer per platform. The installer puts `proofrunkit` and the short `prk` command on your
PATH. After that, `prk -update` keeps it current.

## Downloads

| Platform | Architecture | Status | Download |
| --- | --- | --- | --- |
| **Windows** | x64 (64-bit) | ✅ Available — v0.1.0 | [**Latest release**](https://github.com/Patrick-W-McMahon/ProofRunKit/releases/latest) · [ProofRunKit-0.1.0-win-x64.msi](https://github.com/Patrick-W-McMahon/ProofRunKit/releases/download/v0.1.0/ProofRunKit-0.1.0-win-x64.msi) · [.sha256](https://github.com/Patrick-W-McMahon/ProofRunKit/releases/download/v0.1.0/ProofRunKit-0.1.0-win-x64.msi.sha256) |
| **Windows** | x86 (32-bit) | ⏸️ On hold ([#74](https://github.com/Patrick-W-McMahon/ProofRunKit/issues/74)) | Coming soon |
| **macOS** | Apple Silicon / Intel | 🗓️ Planned ([#35](https://github.com/Patrick-W-McMahon/ProofRunKit/issues/35)) | Coming soon |
| **Linux** | x64 / x86 | 🗓️ Planned ([#34](https://github.com/Patrick-W-McMahon/ProofRunKit/issues/34)) | Coming soon |

**Latest release** always points to the newest version. The direct links are for v0.1.0.

## Windows x64

### 1. Download and verify

Download the `.msi` and its `.sha256` file from the [latest release](https://github.com/Patrick-W-McMahon/ProofRunKit/releases/latest),
then compare the checksums in PowerShell:

```powershell
cd $HOME\Downloads
$expected = (Get-Content .\ProofRunKit-0.1.0-win-x64.msi.sha256).Split(' ')[0]
$actual   = (Get-FileHash .\ProofRunKit-0.1.0-win-x64.msi -Algorithm SHA256).Hash
if ($actual -eq $expected) { 'Checksum OK' } else { 'Checksum MISMATCH: do not install' }
```

`-eq` ignores case, so the lowercase hash in the `.sha256` file matches the uppercase output of `Get-FileHash`.

### 2. Install

Double-click the MSI, or run it from a terminal:

```powershell
msiexec /i ProofRunKit-0.1.0-win-x64.msi            # with the installer UI
msiexec /i ProofRunKit-0.1.0-win-x64.msi /passive   # progress bar only
```

| What | Where |
| --- | --- |
| Program files | `%ProgramFiles%\ProofRunKit` (usually `C:\Program Files\ProofRunKit`) |
| PATH | The install folder is added to the system PATH (the **AddToPath** feature, on by default) |
| Start Menu | A **ProofRunKit** folder with Help and a link to this repository |
| Scope | Per-machine (needs administrator approval) |

> **Unsigned installer.** The MSI and `proofrunkit.exe` are not code-signed yet ([#41](https://github.com/Patrick-W-McMahon/ProofRunKit/issues/41)).
> Microsoft SmartScreen may show *"Windows protected your PC"*. Check the checksum first, then choose
> **More info → Run anyway**. Antivirus tools such as Norton may also ask about the file the first time it runs.

### 3. Check it works

Open a **new** terminal so it picks up the updated PATH:

```powershell
prk -v                  # version, engines, and whether a newer release exists
prk help                # commands and options
```

Then write a first test script. [docs/lua-tests.md](docs/lua-tests.md) and [docs/js-tests.md](docs/js-tests.md) show how.

### 4. Update

```powershell
prk -update -check      # is a newer release available?
prk -update             # download the latest x64 MSI, verify its SHA-256, and install it
```

`prk -update` downloads the MSI and its `.sha256` from the latest GitHub release, checks the hash, and runs
`msiexec /passive`. The new version replaces the old one in place. Add `-yes` to skip the prompt or `-dry-run` to see
what it would do. See [docs/update.md](docs/update.md) for details.

### 5. Uninstall

Open **Settings → Apps → Installed apps** (or **Apps & features**), find **ProofRunKit**, and choose **Uninstall**.
This also removes the install folder from PATH. Or run `msiexec /x ProofRunKit-0.1.0-win-x64.msi`.

## Build from source

To build ProofRunKit yourself (Visual Studio 2026, CMake 3.21+), see [docs/building-windows.md](docs/building-windows.md).
To build the installers, see [docs/installer-windows-x64.md](docs/installer-windows-x64.md).

---

<p align="center">
  <a href="README.md">README</a> ·
  <a href="CONTRIBUTING.md">Contributing</a> ·
  <a href="LICENSE">License (Apache-2.0)</a> ·
  <a href="https://github.com/Patrick-W-McMahon/ProofRunKit/issues">Issues</a>
</p>
