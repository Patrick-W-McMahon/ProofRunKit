# Report formats

ProofRunKit reports results in three formats ([#21](https://github.com/Patrick-W-McMahon/ProofRunKit/issues/21)).
One format goes to stdout. JSON and JUnit XML reports can also be written to files during the same run, so a single
run can produce all three:

```powershell
prk run --json-file out\results.json --junit-file out\results.xml tests\*.lua   # console + both files
prk run --reporter json tests\*.lua                                             # JSON on stdout
prk run --reporter junit tests\*.lua > results.xml                              # JUnit XML on stdout
```

| Option | Effect |
| --- | --- |
| `--reporter FORMAT` / `--format FORMAT` | Format on stdout: `console` (default), `json`, or `junit`. `--json` is short for `--reporter json`. |
| `--json-file PATH` | Also writes the JSON report to `PATH`, replacing it. |
| `--junit-file PATH` | Also writes the JUnit XML report to `PATH`, replacing it. |

Report files are written after the last test finishes. The folder must already exist. If a file cannot be written,
`prk` prints `prk: cannot write report '<path>': <reason>` and exits with code 5 (see [exit-codes.md](exit-codes.md)).
Report-file options apply only to `run`; they are a usage error with `list`.

Whenever stdout carries `json` or `junit`, script `print(...)` output goes to stderr instead, so the report stays
machine-readable.

## Console

Each test gets one line, and a summary line comes last. This format is for people; tools should read JSON or JUnit
instead.

```text
[ PASS ] math.adds (1520 ticks)
[ FAIL ] math.divides (2210 ticks): tests\math_test.lua:9: values differ: expected 2, got 3
[ SKIP ] math.big_numbers (12 ticks): needs #13
3 tests: 1 passed, 1 failed, 1 skipped
```

## JSON (schema version 1)

```json
{
  "schema_version": 1,
  "tool": "proofrunkit",
  "version": "0.1.1",
  "summary": {"total": 3, "passed": 1, "failed": 1, "skipped": 1},
  "tests": [
    {"name": "math.adds", "outcome": "passed", "elapsed_ticks": 1520, "message": ""},
    {"name": "math.divides", "outcome": "failed", "elapsed_ticks": 2210, "message": "tests\\math_test.lua:9: ..."}
  ]
}
```

| Field | Type | Meaning |
| --- | --- | --- |
| `schema_version` | integer | Layout version. It changes only for incompatible changes. New fields may be added without a bump, so consumers must ignore unknown fields. |
| `tool` | string | Always `"proofrunkit"`. |
| `version` | string | ProofRunKit version (semantic version). |
| `summary.total` / `passed` / `failed` / `skipped` | integer | Totals for the selected tests. `total = passed + failed + skipped`. |
| `tests[]` | array | One entry per selected test, in run order. |
| `tests[].name` | string | Full test name, for example `suite.inner.test` for Lua suites. Names are unique within a run. |
| `tests[].outcome` | string | `"passed"`, `"failed"`, or `"skipped"`. |
| `tests[].elapsed_ticks` | integer | Time spent in the test, in ticks of the run's clock (the CPU cycle counter for `prk`). Use it to compare tests within one run, not as seconds. |
| `tests[].message` | string | Failure or skip message (may contain newlines and a stack traceback). Empty when the test passed. |

The JSON is UTF-8. Control characters are escaped as `\uXXXX`.

## JUnit XML

The layout is the common Jenkins/Ant JUnit format, which most CI systems and test-report viewers read:

```xml
<?xml version="1.0" encoding="UTF-8"?>
<testsuites name="proofrunkit" tests="3" failures="1" errors="0" skipped="1" time="0.004">
  <testsuite name="proofrunkit" tests="3" failures="1" errors="0" skipped="1" time="0.004">
    <properties>
      <property name="proofrunkit.version" value="0.1.1"/>
    </properties>
    <testcase classname="math" name="adds" time="0.001"/>
    <testcase classname="math" name="divides" time="0.002">
      <failure message="tests\math_test.lua:9: values differ: expected 2, got 3" type="failure">...full message...</failure>
    </testcase>
    <testcase classname="math" name="big_numbers" time="0.000">
      <skipped message="needs #13"/>
    </testcase>
  </testsuite>
</testsuites>
```

| Item | Mapping |
| --- | --- |
| `classname` / `name` | The test name split at its last `.`: `suite.inner.test` becomes `classname="suite.inner"` and `name="test"`. A name without a dot gets `classname="proofrunkit"`. |
| `time` | Wall-clock seconds measured by the reporter (millisecond precision). Suite times are the sum of the test times. |
| `<failure>` | Failed tests. The `message` attribute holds the first line of the message, and the element text holds the full message. |
| `<skipped>` | Skipped tests, with the skip reason. |
| `errors` | Always `0`. Tool errors such as script load failures stop the run before any test runs, and are reported through the exit code instead. |

Text is escaped for XML 1.0. Control characters that XML forbids are replaced with `?`.
