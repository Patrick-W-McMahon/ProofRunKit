# Updating ProofRunKit: `prk -update` and `prk -version`

Status: implemented ([#75](https://github.com/Patrick-W-McMahon/ProofRunKit/issues/75)); there is no published release
yet, so for now both commands report "no release has been published yet".

## `prk -version`

```text
> prk -version
ProofRunKit 0.1.1 (cycle counter: asm-masm-x64-windows, Lua 5.4.9, QuickJS-ng 0.18.0)
Latest release: 0.2.0 - update available, run 'prk -update'
```

`-version`, `-v`, `--version`, and `version` are the same command. The first line is unchanged from earlier versions.
The second line comes from the GitHub Releases API and is one of:

- `Latest release: X.Y.Z - up to date`
- `Latest release: X.Y.Z - update available, run 'prk -update'`
- `Latest release: X.Y.Z - this build is newer`
- `Could not check for updates: REASON` (no network, no release published yet, GitHub rate limit, unsupported platform)

The exit code is always 0. `-offline` / `--offline`, or the environment variable `PRK_OFFLINE=1` (any value other
than empty or `0`), skips the lookup and prints only the first line. CTest sets `PRK_OFFLINE=1` for every test, and the
self-test binary contains no network code at all, so tests never touch the network.

## `prk -update`

```text
> prk -update
Current version: 0.1.1
Latest release:  0.2.0 (v0.2.0)
Downloading ProofRunKit-0.2.0-win-x64.msi...
SHA-256 verified: 5f1c...
Install ProofRunKit 0.2.0 now? [y/N] y
Started the installer (msiexec /passive). ProofRunKit exits now so it can be replaced.
```

`-update`, `--update`, and `update` are the same command. Options take one or two dashes:

| Option | Meaning |
| --- | --- |
| `-check` | Only print the current and latest versions and whether an update is available. Nothing is downloaded. |
| `-yes` | Install without asking. |
| `-dry-run` | Download and verify, print the `msiexec` command, and stop. Nothing is saved or started. |

Steps:

1. GET `https://api.github.com/repos/Patrick-W-McMahon/ProofRunKit/releases/latest` (WinHTTP, system proxy,
   `User-Agent: ProofRunKit/<version>`). Pre-releases and drafts are never "latest".
2. Parse `tag_name` (`vX.Y.Z` or `X.Y.Z`, optional `-pre`) and compare it with this build. Not newer: "up to date", exit 0.
3. Choose the MSI: the first asset whose name starts with `ProofRunKit-`, contains `x64`, ends in `.msi`, uses only
   letters, digits, `.`, `_`, `-`, and has an `https://` URL. Both `ProofRunKit-0.2.0-win-x64.msi` (what
   `installer/build-msi-x64.ps1` produces) and `ProofRunKit-x64-0.2.0.msi` match.
4. Require the asset `<msi name>.sha256` containing the 64-digit SHA-256 (optionally followed by the file name, as
   `sha256sum` writes). **Without it, or when it does not match the downloaded MSI, the update is refused** (exit 6)
   and nothing is written.
5. Ask for confirmation (unless `-yes`), write the MSI to `%TEMP%\<msi name>`, start
   `%SystemRoot%\System32\msiexec.exe /i "<msi>" /passive` detached, and exit 0. The MSI's `MajorUpgrade` (same
   UpgradeCode) replaces the installed version while showing a progress bar.

Exit codes: 0 for up to date, check-only, dry run, cancelled, or installer started; 6 for any failure (no release,
network error, missing asset or checksum, checksum mismatch, unsupported platform). See [exit-codes.md](exit-codes.md).

Platforms: Windows x64 only. On x86 Windows builds the check works but installing is refused (x86 is deferred,
[#74](https://github.com/Patrick-W-McMahon/ProofRunKit/issues/74)); on Linux and macOS both say "not supported on this
platform yet".

## Publishing a release (what `prk -update` needs)

1. Set the version in `CMakeLists.txt` (`project(... VERSION X.Y.Z)`), merge to `main`.
2. Run `.\installer\build-msi-x64.ps1`. It writes `build\windows-msvc-x64-release\installer\ProofRunKit-X.Y.Z-win-x64.msi`
   **and** `ProofRunKit-X.Y.Z-win-x64.msi.sha256`. Then `.\installer\verify-msi-x64.ps1` also checks the checksum.
3. Create the GitHub release with tag `vX.Y.Z` (not a pre-release, not a draft) and attach **both** files unchanged.
   The asset names must stay as built (or follow `ProofRunKit-x64-X.Y.Z.msi` + `.sha256`).
4. Check from an installed older version: `prk -version`, then `prk -update -check`, then `prk -update`.

## Design

- `src/cpp/update/update_command.{hpp,cpp}`: the command, driven entirely through `UpdateServices` (HTTP GET,
  download folder, file writer, installer launcher, answer reader).
- `src/cpp/update/release_info.{hpp,cpp}` and `json_reader.{hpp,cpp}`: pure parsing and version logic.
- `include/proofrunkit/prk_sha256.h`, `src/core/prk_sha256.c`: SHA-256 (FIPS 180-4).
- `src/cpp/update/system_update_services.cpp`, `system_update_windows.cpp`: the real services (WinHTTP, `%TEMP%`,
  `msiexec`), in the `prk_update_system` library linked only into `proofrunkit.exe`.
- Self-tests `update.*` cover SHA-256 vectors, JSON, versions, assets, checksum files, and every command path with a
  fake HTTP layer.

Not covered yet: code signing / Authenticode checks (the checksum proves the file matches the release, not who built
it; see [#41](https://github.com/Patrick-W-McMahon/ProofRunKit/issues/41)), and resuming partial downloads.
