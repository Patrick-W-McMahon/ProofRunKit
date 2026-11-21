# Exit codes

`prk` (`proofrunkit.exe`, also through the `prk.cmd` shim) uses these exit codes ([#21](https://github.com/Patrick-W-McMahon/ProofRunKit/issues/21)).
Scripts and CI can rely on them. The self-tests and the `prk_reports_and_exit_codes` CTest and the `self.cli` entry of `prk.tests.json` check every code end to end.

| Code | Name | Meaning |
| --- | --- | --- |
| 0 | all passed | Every selected test passed. Skipped tests do not fail a run, and a run that selects no tests also exits 0. `list`, `version`, and `help` exit 0. |
| 1 | test failed | At least one selected test failed. |
| 2 | usage error | The command line was invalid: an unknown option or command, a missing or bad option value, an unsupported script extension, `--tag` / `--exclude-tag` / `--timeout` without scripts, or report files with `list`. |
| 3 | not implemented | Reserved for a requested feature that has not landed yet. No shipped feature returns it: JavaScript test scripts, its first use, run since [#11](https://github.com/Patrick-W-McMahon/ProofRunKit/issues/11). |
| 4 | script error | A test script could not be read, compiled, or loaded (missing file, syntax error, error in top-level code, duplicate test name, bad test options), or `prk test` found an invalid `prk.tests.json`. No tests are run. |
| 5 | report error | A `--json-file` or `--junit-file` report could not be written. The tests did run. |
| 6 | update failed | `prk -update` could not finish: no release, network error, missing x64 MSI or `.sha256`, checksum mismatch, or an unsupported platform ([update.md](update.md)). `prk -version` never returns it. |

Codes 2 to 6 are tool errors, and they take precedence over code 1. For example, a run where a test fails and the
JUnit file cannot be written exits with 5. Treat any non-zero code as "do not trust this run as green", and treat
only 1 as "the code under test is broken".
