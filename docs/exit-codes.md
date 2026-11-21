# Exit codes

`prk` and `proofrunkit` use these exit codes ([#21](https://github.com/Patrick-W-McMahon/ProofRunKit/issues/21)).
Scripts and CI can rely on them. The self-tests and the `prk_reports_and_exit_codes` CTest check every code end to end.

| Code | Name | Meaning |
| --- | --- | --- |
| 0 | all passed | Every selected test passed. Skipped tests do not fail a run, and a run that selects no tests also exits 0. `list`, `version`, and `help` exit 0. |
| 1 | test failed | At least one selected test failed. |
| 2 | usage error | The command line was invalid: an unknown option or command, a missing or bad option value, an unsupported script extension, `--tag` / `--exclude-tag` / `--timeout` without scripts, or report files with `list`. |
| 3 | not implemented | A requested feature has not landed yet, for example JavaScript test scripts ([#11](https://github.com/Patrick-W-McMahon/ProofRunKit/issues/11)). |
| 4 | script error | A test script could not be read, compiled, or loaded (missing file, syntax error, error in top-level code, duplicate test name, bad test options). No tests are run. |
| 5 | report error | A `--json-file` or `--junit-file` report could not be written. The tests did run. |

Codes 2 to 5 are tool errors, and they take precedence over code 1. For example, a run where a test fails and the
JUnit file cannot be written exits with 5. Treat any non-zero code as "do not trust this run as green", and treat
only 1 as "the code under test is broken".
