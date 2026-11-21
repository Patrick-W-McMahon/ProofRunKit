# Assembler pass

Status: done ([#16](https://github.com/Patrick-W-McMahon/ProofRunKit/issues/16)). ProofRunKit assembles one source
file into an object file with a real assembler: Microsoft MASM (`ml64`, or `ml` for x86 targets), the GNU assembler
(`as`, also MinGW-w64's), or NASM. The object can then be linked with `prk.compile` and run with `prk.run_process`, so
a test sees the exit code, stdout, stderr, and timing of every step. It never assembles in-process.

- C API: [`include/proofrunkit/prk_asm_driver.h`](../include/proofrunkit/prk_asm_driver.h)
  (`prk_assemble_source`, `prk_choose_assembler_family`).
- Source: [`src/drivers/prk_asm_driver.c`](../src/drivers/prk_asm_driver.c). Diagnostics use the C driver's parsers
  ([c-cpp-driver.md](c-cpp-driver.md)): MSVC style for MASM, GCC style for GAS and NASM.
- Script bindings: `prk.assemble` in [`src/script/prk_lua_compile.c`](../src/script/prk_lua_compile.c) and
  [`src/script/prk_js_assemble.c`](../src/script/prk_js_assemble.c).
- Everything runs through the spawn layer's `PrkProcessRunner` ([process-spawn.md](process-spawn.md)).

## From a test script

```lua
prk.add_test("asm.add_numbers", function()
    local object = prk.assemble{source = "src/add.asm", output = "build/add.obj", werror = true}
    if object.outcome == "failed to start" then prk.skip_test(object.error) end
    prk.expect_exit_code(object, 0)
    local program = prk.compile{sources = {"src/main.c", object.output}, output = "build/add"}
    prk.expect_exit_code(program, 0)
    prk.expect_exit_code(prk.run_process{program = program.output}, 0)
end)
```

```js
prk.addTest('asm.addNumbers', () => {
    const object = prk.assemble({source: 'src/add.s', output: 'build/add.o'});
    prk.expectExitCode(object, 0);
});
```

| Option (Lua / JavaScript) | Meaning |
| --- | --- |
| `source` | Required. One assembly source. |
| `output` | Required. Object file to write (`/Fo` for MASM, `-o` otherwise); used as given. |
| `assembler` | `"auto"` (default), `"masm"`, `"gas"`, or `"nasm"`. AUTO picks by extension: `.s` / `.S` → GAS, `.nasm` → NASM, `.asm` → MASM on Windows and NASM elsewhere. Any other extension needs `assembler`. |
| `program` | Assembler program override, e.g. `"x86_64-w64-mingw32-as"` or `"C:\\nasm\\nasm.exe"`; needs an `assembler` family. |
| `includes`, `defines`, `flags` | Include folders (`/I`, `-I`), symbols (`/D` and `-D`; GAS gets `--defsym NAME=VALUE`, and a bare `NAME` means `NAME=1`), and extra flags passed unchanged at the end. |
| `werror` | `true` adds `/WX` (MASM), `--fatal-warnings` (GAS), or `-Werror` (NASM). MASM always runs at `/W3`. |
| `format` | NASM `-f` format; default `win64` / `win32` on Windows, `macho64` on macOS, `elf64` / `elf32` elsewhere, matching this ProofRunKit build. |
| `msvc_arch` / `msvcArch` | vcvarsall architecture for MASM discovery (`x64`, `x86`, `arm64`, or a pair such as `x64_x86`). Targets ending in `x86` use `ml.exe`, others `ml64.exe`. Default: this build's architecture. |
| `cwd` | Folder the assembler runs in. |
| `timeout_ms` / `timeoutMs` | Assembler timeout; default 60000, never longer than the time left in the test. |

The result is the `prk.compile` result ([c-cpp-driver.md](c-cpp-driver.md)) with `assembler` (`"masm"`, `"gas"`,
`"nasm"`) instead of `compiler` (which is `nil` / `undefined`), `kind` = `"object"`, and the full `command` line.

## Finding MASM

MASM is tried on PATH first (`ml64` or `ml`, as in a Developer Prompt). If it is not there, ProofRunKit uses the same
MSVC discovery as the C driver (vswhere, then vcvarsall; see [c-cpp-driver.md](c-cpp-driver.md#msvc-discovery-vswhere-and-vcvarsall)) and
runs the `ml64.exe` / `ml.exe` next to the discovered `cl.exe` with the vcvars environment. The discovery runs once per
`prk run` and is shared with `prk.compile`. `msvc_discovered` / `msvcDiscovered` is `true` when it was used.

## Missing assemblers

A missing assembler is returned, not raised: `outcome` is `"failed to start"` and `error` names the program (for MASM:
`ml64 is not on PATH and MSVC discovery failed: ...`). Nothing reports success without running a real assembler, so a
test can `prk.skip_test` on a machine without one. On Linux and macOS GAS and NASM work wherever they are installed;
MASM is Windows only.

## Tests

- Self-test `asm_driver.*` (7 tests): command lines, family choice, MASM / GAS / NASM diagnostics, a fake runner
  (including the MASM fallback to discovery and `ml.exe` for x86), the Lua and JavaScript bindings, and a real
  assemble, link, and run of an `add` function (MASM x64 or x86 on Windows, GAS x86-64 elsewhere; skipped when the
  assembler is missing or the host is arm64).
- The `prk.tests.json` entries `self.assemble` and `js.assemble` run [`tests/lua/self/assemble_test.lua.in`](../tests/lua/self/assemble_test.lua.in)
  and [`tests/js/assemble_test.js.in`](../tests/js/assemble_test.js.in) over the samples in
  [`tests/drivers/asm`](../tests/drivers/asm); each test skips when its assembler is missing.
