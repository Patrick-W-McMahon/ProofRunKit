# Vendored Lua 5.4.9

| Field | Value |
| --- | --- |
| Upstream | https://www.lua.org/ftp/lua-5.4.9.tar.gz |
| Released | 2026-08-10 |
| SHA-256 of the tarball | `2335b6c582a52654f94612bf10d2f4672805d05329aa6568b1d8cd9e5c6fb8e6` |
| License | MIT (see [LICENSE](LICENSE), copied from the notice at the end of `src/lua.h`) |
| Tracking issue | [#9](https://github.com/Patrick-W-McMahon/ProofRunKit/issues/9) |

`src/` and `README` are the upstream files **unmodified** (only `src/Makefile` and the `doc/` folder are
omitted). Do not edit them; ProofRunKit's sandbox lives in `src/script/` and controls Lua only through the public
C API.

## What is compiled

`cmake/ProofRunKitLua.cmake` builds a static library from the core and the safe standard libraries only:
base, coroutine, table, string, math, and utf8. These upstream files are **not compiled** into the
executable, so the code for them is not present at all:

| File | Why it is excluded |
| --- | --- |
| `lua.c`, `luac.c` | Stand-alone interpreter and compiler programs. |
| `linit.c` | `luaL_openlibs` would open every library; ProofRunKit opens a fixed safe set itself. |
| `liolib.c` | `io` library: ambient file system access. |
| `loslib.c` | `os` library: process, environment, clock, and file access (`os.execute`, `os.remove`, ...). |
| `loadlib.c` | `package` / `require`: loads files and native DLLs. |
| `ldblib.c` | `debug` library: could remove the instruction-limit hook and break the sandbox. |

## Updating

1. Download the new 5.4.x tarball from https://www.lua.org/ftp/ and verify its SHA-256 against the page.
2. Replace `src/` and `README`, delete `src/Makefile`, rename this folder, and update the version in
   `cmake/ProofRunKitLua.cmake`, this file, and the repository `NOTICE`.
3. Re-run the full build and self-test on Windows x64 and x86.
