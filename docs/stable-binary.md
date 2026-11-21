# Keeping the ProofRunKit binary stable (#22)

The project exists to stop shipping a new custom test executable on every change, which antivirus
tools such as Norton treat as a new unknown file. This page records how the build keeps
`proofrunkit.exe` stable. It is not a substitute for the verification plan in #22.

## What is in place now

- **Tests are never compiled into the binary.** `proofrunkit` contains the core, the CLI, and a
  small set of built-in environment checks (`kit.*`). User tests are loaded at runtime as Lua
  or JavaScript scripts (#9, #11; [lua-tests.md](lua-tests.md), [js-tests.md](js-tests.md)).
- **No self-modifying code, no packers, no self-extraction.** The build uses the plain MSVC
  toolchain output. Do not run UPX or any other packer on the executable.
- **VERSIONINFO resource and application manifest** are embedded in every Windows executable
  (`src/windows/proofrunkit.rc.in`, `src/windows/proofrunkit.manifest`). The manifest requests
  `asInvoker` (never elevated), declares Windows 10/11 support, UTF-8, and long-path awareness.
- **Reproducible builds.** `PRK_REPRODUCIBLE_BUILD=ON` (the default) passes `/Brepro` to the MSVC
  compiler and linker, so the same sources and toolchain produce the same bytes (no embedded
  timestamps). Rebuilding without source changes should not create a "new" file.

## Still to do (tracked on #22)

- Fixed working directory for generated artifacts (`%LOCALAPPDATA%\ProofRunKit\work\<project>`) once
  the compiler drivers (#13-#16) exist, and loading code under test as a DLL where possible.
- A documented, folder-scoped Norton exclusion as an **option only**, applied with Patrick's approval.
- The Windows verification run: same binary, tests changed, confirm Norton does not re-flag it.
- Code signing stays off unless Patrick approves the spend (#41).
