# Project test configuration: `prk.tests.json` and `prk test`

`prk test` runs a whole project's tests from one checked-in file, `prk.tests.json`, and prints one combined
summary and report ([#82](https://github.com/Patrick-W-McMahon/ProofRunKit/issues/82)). Each entry gets its own
working folder, arguments (for example tags), environment, expected exit code, and expected output. Setup steps can
generate fixtures first. Variables such as `${build_dir}` replace build-system substitution, so the same file works
from CTest, from a terminal, and with the installed `prk`.

```powershell
prk test                                       # ./prk.tests.json, everything that needs no build folder
prk test --build-dir build\windows-msvc-x64-debug
prk test --filter self. --junit-file out\tests.xml
prk test --list
```

## Why JSON

- **Declarative.** Discovering the tests never runs project code, and the file can be reviewed like data.
- **Readable by other tools.** IDEs, CI, and scripts can read and edit it without embedding Lua.
- **Same family as the reports.** It matches the JSON report format, and prk already includes a strict JSON reader.
- **No second sandbox.** A Lua configuration would need a second sandbox mode that may describe processes and
  folders.

Test logic stays in Lua and JavaScript test scripts. The configuration only says how to run them.

## Schema (version 1)

```json
{
  "schema_version": 1,
  "description": "free text",
  "variables": {
    "bin_dir": ["${build_dir}/Debug", "${build_dir}"],
    "runner": ["${bin_dir}/proofrunkit${exe}", "${prk}"]
  },
  "defaults": { "env": { "PRK_OFFLINE": "1" }, "timeout_ms": 600000 },
  "runner": "${runner}",
  "work_dir": "${build_dir}/prk-test",
  "setup": [ { "name": "fixture", "run": "tests/fixture.lua", "args": ["--json-file", "${work_dir}/f.json"],
               "expect_exit_code": 1 } ],
  "tests": [
    { "name": "unit", "run": ["tests/a_test.lua", "tests/b_test.js"] },
    { "name": "tagged", "run": "tests/tags_test.lua", "args": ["--tag", "fast"] },
    { "name": "cli", "run": "tests/cli_test.js.in", "template": true, "cwd": "${bin_dir}" },
    { "name": "version", "command": ["${bin_dir}/app${exe}", "--version"], "expect_output": "App \\d+" },
    { "name": "native", "command": "${bin_dir}/unit_tests${exe}", "report": "json",
      "skip_unless": "${bin_dir}/unit_tests${exe}" }
  ]
}
```

| Key | Meaning |
| --- | --- |
| `schema_version` | Required, `1`. |
| `variables` | Name → string, or list of candidate paths (the first that exists wins). Variables may use each other. |
| `defaults.env` | Environment set for every entry, on top of the inherited one. |
| `defaults.timeout_ms` | Per-entry wall-clock limit (default 600000). The process tree is killed when it passes. |
| `runner` | The prk that runs `run` entries (default `${prk}`, the prk running `prk test`). |
| `work_dir` | Folder for templates and child reports (default `${build_dir}/prk-test`, or `.prk-test` in the config folder). |
| `setup` | Entries run first, in order. The first failure stops the run and is reported as `setup/<name>`. |
| `tests` | Test entries, in order. Unknown keys anywhere are errors, so typos are caught. |

### Entry keys

| Key | Meaning |
| --- | --- |
| `name` | Unique; letters, digits, `_`, `-`, `.`. Results are named `<name>/<test>` or `<name>`. |
| `run` | Script or scripts, run as `<runner> run <scripts> <args>`. Exactly one of `run` / `command`. |
| `command` | Program and arguments, started directly (no shell). A bare name is looked up on PATH. |
| `args` | Extra arguments after the scripts or command. |
| `cwd` | Working folder (default: the config folder). |
| `env` | Environment for this entry, over `defaults.env`. |
| `expect_exit_code` | Expected exit code (default 0). |
| `expect_output` / `reject_output` | ECMAScript regular expressions searched in stdout + stderr. |
| `timeout_ms` | Overrides `defaults.timeout_ms`. |
| `template` | `true`: expand `${name}` inside the scripts, write them to `${work_dir}/templates/<entry>/` (dropping `.in`), and run those. `$${` stays a literal `${`. |
| `report` | `"json"`: append `--json-file` and list every test of the child's JSON report in the combined results. `"none"`: one result for the entry. Default: `"json"` for `run` entries that expect exit 0. |
| `skip_unless` | Path or paths that must exist, otherwise the entry is skipped with a reason. |
| `make_dirs` | Folder or folders created (with parents) before the entry starts, for example for report files. |
| `description` | Free text. |

Relative paths in `run`, `command` (when they contain a separator), `cwd`, and `skip_unless` start at the config
folder.

## Variables

| Variable | Value |
| --- | --- |
| `${prk}` | Absolute path of the running executable, `proofrunkit.exe` (also when started as `prk` through `prk.cmd`). |
| `${prk_dir}` | Folder of the running prk (for example `C:/Program Files/ProofRunKit`). |
| `${config_dir}` | Folder of `prk.tests.json`. |
| `${build_dir}` | `--build-dir DIR`, made absolute. Unset without it. |
| `${work_dir}` | See `work_dir` above. |
| `${exe}` | `.exe` on Windows, empty elsewhere. |
| `${os}` | `windows`, `linux`, or `macos`. |

Built-in paths use forward slashes on every platform (Windows accepts them), so they can be pasted into Lua and
JavaScript string literals by templates without escaping.

`--var NAME=VALUE` sets or overrides any variable (repeatable). An entry that uses an unset variable is **skipped**
with `needs ${name}`, so `prk test` without `--build-dir` still runs everything that does not need a build.

## Results and exit codes

- `prk test` prints one progress line per entry (on stderr with `--json` / `--reporter junit`), then the combined
  results through the usual reporters: console, `--json`, `--reporter junit`, `--json-file`, and `--junit-file`.
- Times in the combined report are milliseconds per entry. Tests read from a child report show 0.
- Exit codes: 0 all passed (skips allowed), 1 a test failed, 2 usage error or no config, 4 invalid config,
  5 a report file could not be written.

## Migrating a project

1. List each existing test command as an entry: `run` for prk scripts, `command` for anything else.
2. Move working folders, arguments, environment, and expected exit codes and output from the old runner (for
   example CTest properties) into the entry.
3. Replace build-system substitution (`configure_file`, `@VAR@`) with `template: true` and `${var}`.
4. Turn fixture steps into `setup` entries that write into `${work_dir}`.
5. Let the old runner call `prk test` once, and keep the old tests until the new run is confirmed equivalent.
