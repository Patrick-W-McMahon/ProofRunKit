# Vendored QuickJS-ng 0.18.0

| Field | Value |
| --- | --- |
| Upstream | https://github.com/quickjs-ng/quickjs/archive/refs/tags/v0.18.0.tar.gz |
| Released | 2026-11-01 |
| SHA-256 of the tarball | `28bcd89aa3683fd2a946537e5ce3f11cc46b5cbf3383c7561aaef3df5ff9f1dd` |
| License | MIT (see [LICENSE](LICENSE), the upstream file) |
| Tracking issue | [#11](https://github.com/Patrick-W-McMahon/ProofRunKit/issues/11) |

The files in this folder are the upstream files **unmodified** (byte-for-byte; compare them with the tarball).
Do not edit them. ProofRunKit's sandbox lives in `src/script/prk_js_*.c` and controls the engine only through
the public C API in `quickjs.h`.

## What is compiled

`cmake/ProofRunKitQuickJs.cmake` builds the static library `prk_quickjs` from four files: `quickjs.c`,
`libregexp.c`, `libunicode.c`, and `dtoa.c`. The other files here are headers they include.

These upstream files are **not vendored at all**, so their code is not in the executable:

| File or folder | Why it is excluded |
| --- | --- |
| `quickjs-libc.c`, `quickjs-libc.h` | The `qjs:std` / `qjs:os` modules: files, processes, environment, timers, workers. |
| `qjs.c`, `qjsc.c`, `repl.js`, `standalone.js`, `qjs-wasi-reactor.c` | The stand-alone interpreter, compiler, and REPL. |
| `*.js` sources of the `builtin-*.h` headers, `gen/`, `amalgam.js` | Generator inputs; the generated headers are vendored. |
| `test262/`, `tests/`, `run-test262.c`, `api-test.c`, `ctest.c`, `cxxtest.cc`, `lre-test.c`, `fuzz.c` | Upstream tests. |
| `docs/`, `examples/`, `*.man`, `SECURITY.md` | Documentation and samples. |
| `CMakeLists.txt`, `Makefile`, `meson.build`, `meson_options.txt` | Upstream build files; ProofRunKit uses its own. |

## Build settings

Mirroring upstream's own CMake: C11 with extensions, `QUICKJS_NG_BUILD` and `_GNU_SOURCE`, plus
`WIN32_LEAN_AND_MEAN` and `_WIN32_WINNT=0x0601` on Windows. GCC/Clang use `-funsigned-char` and `-w`;
MSVC uses `/W1`, `/wd4098` (see below), `/experimental:c11atomics` (needed for `<stdatomic.h>`; probed with
`check_c_compiler_flag`), `_CRT_SECURE_NO_WARNINGS`, and `_CRT_NONSTDC_NO_DEPRECATE`. Upstream warnings are
not ProofRunKit's to fix, so the project's `-Werror` / `/WX` settings are not applied to this target.

## Suppressed warnings

| Compiler | Warning | Where | Why it is suppressed, not fixed |
| --- | --- | --- | --- |
| MSVC | C4098 "'void' function returning a value" | `quickjs.c`, the `JS_FreeCString*` helpers (lines 5011-5026 in 0.18.0) | Upstream returns a void call from a void function. It is harmless and the file stays unmodified, so `/wd4098` is set on the `prk_quickjs` target only ([#70](https://github.com/Patrick-W-McMahon/ProofRunKit/issues/70)). Project code still gets C4098 at `/W4 /WX`. |

Re-check this table when updating: drop a suppression once upstream no longer triggers it.

## Updating

1. Download the new release tarball from https://github.com/quickjs-ng/quickjs/releases and record its SHA-256.
2. Copy the files listed in this folder (no others), rename the folder, and update the version in
   `cmake/ProofRunKitQuickJs.cmake`, `src/script/prk_js_script.c` (the engine version string is built from
   `QJS_VERSION_*`), the self-tests that check `QuickJS-ng 0.18.0`, this file, and the repository `NOTICE`.
3. Re-run the full build and self-test on Windows x64 and x86.
