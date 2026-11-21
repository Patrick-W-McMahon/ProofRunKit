# Self-hosted testing: coverage map

Status: in progress ([#55](https://github.com/Patrick-W-McMahon/ProofRunKit/issues/55)). ProofRunKit tests itself
with its own Lua and JavaScript test scripts, run by `prk` through CTest, alongside the C++ harness
`proofrunkit_selftest`. **The C++ self-test stays**: retiring it needs the self-hosted suite to cover every case that
can be expressed through scripts, a green run on Windows, and Patrick's explicit OK (#55, #27).

## Running the self-hosted suite

Everything runs from CTest (`ctest --preset <preset>`); each script can also be run by hand:

```powershell
prk run tests\lua\self\assertions_test.lua tests\lua\self\suites_and_hooks_test.lua tests\lua\self\tags_test.lua
prk run tests\lua\self\sandbox_test.lua tests\js\self\sandbox_test.js tests\lua\prk_lua_api_test.lua tests\js\prk_js_api_test.js
```

Scripts ending in `.in` (compile, assemble, CLI) get folder paths from CMake and run from the build tree. The
command-line suite `tests/js/self/cli_test.js.in` runs `./prk` itself, with `-offline` / `PRK_OFFLINE=1`, so no
test touches the network.

A single `prk run tests/` entry point is not added yet: `prk run` takes script files, not folders, and several
scripts need CMake-provided paths and fixture runs first. Folder discovery would be a CLI feature with its own issue.

## Summary

| Status | C++ cases | Meaning |
| --- | ---: | --- |
| Self-hosted | 72 | A prk script (or a CTest that runs prk) checks the same behaviour. |
| Partly | 24 | The observable behaviour is checked through prk; part of the case (limits, fakes, exact internals) is C++ only. |
| Internal only | 52 | Pure C/C++ internals, NULL-argument checks, fake runners, or injected services; not reachable from a script. |
| **Total** | **148** | Every case in `proofrunkit_selftest --list`. |

## Case by case

| C++ self-test case | Status | Self-hosted equivalent or reason |
| --- | --- | --- |
| `core.registry.new_registry_is_empty` | Internal only | C API of the registry (NULL arguments, capacity growth); scripts only see a working registry. |
| `core.registry.registers_in_order_and_copies_names` | Internal only | C API of the registry (NULL arguments, capacity growth); scripts only see a working registry. |
| `core.registry.rejects_duplicate_names` | Self-hosted | tests/js/self/cli_test.js.in `runner.script_problems_exit_4_or_2` |
| `core.registry.rejects_invalid_arguments` | Internal only | C API of the registry (NULL arguments, capacity growth); scripts only see a working registry. |
| `core.registry.grows_past_initial_capacity` | Internal only | C API of the registry (NULL arguments, capacity growth); scripts only see a working registry. |
| `core.registry.destroy_accepts_null` | Internal only | C API of the registry (NULL arguments, capacity growth); scripts only see a working registry. |
| `core.runner.runs_in_order_and_counts_outcomes` | Self-hosted | tests/js/self/cli_test.js.in `runner.runs_in_order_and_counts_outcomes`; tests/lua/self/reports_test.lua |
| `core.runner.filter_selects_matching_names` | Self-hosted | tests/js/self/cli_test.js.in `runner.filter_selects_matching_names`; tests/lua/self/tags_test.lua |
| `core.runner.uses_injected_clock_for_elapsed_ticks` | Internal only | Injected clock, user state, NULL checks, and the fixed message buffer of the C runner. |
| `core.runner.first_failure_message_is_kept` | Self-hosted | tests/js/self/cli_test.js.in `runner.keeps_the_first_failure_message` |
| `core.runner.long_messages_are_truncated` | Internal only | Injected clock, user state, NULL checks, and the fixed message buffer of the C runner. |
| `core.runner.passes_user_state_and_runs_silently` | Internal only | Injected clock, user state, NULL checks, and the fixed message buffer of the C runner. |
| `core.runner.rejects_null_registry` | Internal only | Injected clock, user state, NULL checks, and the fixed message buffer of the C runner. |
| `core.runner.name_selection_rules` | Self-hosted | tests/js/self/cli_test.js.in `runner.filter_selects_matching_names`; tests/lua/self/tags_test.lua |
| `core.reporting.summary_adds_outcomes_without_mutating_input` | Partly | tests/js/self/cli_test.js.in (summary totals); immutability is a C API property. |
| `core.reporting.outcome_and_status_names` | Partly | Outcome names are checked in every JSON report test; status names are C API only. |
| `cli.reporting.console_lines` | Partly | Console lines are matched by the CTest regex tests (prk_lua_reports_failures, prk_lua_failure_message, ...). |
| `cli.reporting.json_escaping` | Self-hosted | tests/js/self/cli_test.js.in `runner.json_escapes_round_trip`; tests/lua/self/reports_test.lua `messages_are_escaped` |
| `cli.reporting.json_report_document` | Self-hosted | tests/lua/self/reports_test.lua `json.*`; tests/js/self/cli_test.js.in (JSON.parse of the whole document) |
| `cli.reporting.console_reporter_writes_through_core` | Partly | Console lines are matched by the CTest regex tests (prk_lua_reports_failures, prk_lua_failure_message, ...). |
| `cli.reporting.json_reporter_writes_one_document` | Self-hosted | tests/lua/self/reports_test.lua `json.*`; tests/js/self/cli_test.js.in (JSON.parse of the whole document) |
| `cli.parse.defaults_with_no_arguments` | Self-hosted | tests/js/self/cli_test.js.in `cli.no_arguments_runs_the_builtin_checks` |
| `cli.parse.parses_flags_values_and_scripts` | Partly | Observed through runs in tests/js/self/cli_test.js.in, tags_test.lua, reports_test.lua, and the CTest timeout tests; the parsed-options struct itself is internal. |
| `cli.parse.reports_usage_errors` | Self-hosted | tests/js/self/cli_test.js.in `cli.usage_errors_exit_2_with_usage_text` |
| `cli.parse.usage_text_names_program_and_options` | Self-hosted | tests/js/self/cli_test.js.in `cli.help_texts` |
| `cli.parse.run_subcommand` | Partly | Observed through runs in tests/js/self/cli_test.js.in, tags_test.lua, reports_test.lua, and the CTest timeout tests; the parsed-options struct itself is internal. |
| `cli.parse.list_version_and_help_subcommands` | Self-hosted | tests/js/self/cli_test.js.in `cli.help_texts`, `cli.version_line_in_every_spelling`, `runner.print_stays_off_json_and_list_output` |
| `cli.parse.reports_subcommand_usage_errors` | Self-hosted | tests/js/self/cli_test.js.in `cli.usage_errors_exit_2_with_usage_text` |
| `cli.parse.flag_form_accepts_json_and_reporter` | Partly | Observed through runs in tests/js/self/cli_test.js.in, tags_test.lua, reports_test.lua, and the CTest timeout tests; the parsed-options struct itself is internal. |
| `cli.parse.tags_and_timeout` | Partly | Observed through runs in tests/js/self/cli_test.js.in, tags_test.lua, reports_test.lua, and the CTest timeout tests; the parsed-options struct itself is internal. |
| `cli.parse.report_options` | Partly | Observed through runs in tests/js/self/cli_test.js.in, tags_test.lua, reports_test.lua, and the CTest timeout tests; the parsed-options struct itself is internal. |
| `cli.help.command_help_text` | Self-hosted | tests/js/self/cli_test.js.in `cli.help_texts` |
| `cli.name.selects_program_name_from_invoked_path` | Self-hosted | tests/js/self/cli_test.js.in `cli.program_name_follows_the_invoked_file` |
| `cli.app.exit_codes` | Self-hosted | tests/js/self/cli_test.js.in (exit codes 0, 1, 2, 4, 5, 6; JSON on stdout, errors on stderr) |
| `cli.app.help_version_and_list` | Self-hosted | tests/js/self/cli_test.js.in `cli.help_texts`, `cli.version_line_in_every_spelling`, `runner.print_stays_off_json_and_list_output` |
| `cli.app.writes_errors_and_json` | Self-hosted | tests/js/self/cli_test.js.in (exit codes 0, 1, 2, 4, 5, 6; JSON on stdout, errors on stderr) |
| `cli.app.runs_subcommands` | Self-hosted | tests/js/self/cli_test.js.in (exit codes 0, 1, 2, 4, 5, 6; JSON on stdout, errors on stderr) |
| `cli.app.prints_command_help_and_errors` | Self-hosted | tests/js/self/cli_test.js.in `cli.help_texts` |
| `hotpath.cycle_counter.reads_are_nonzero` | Internal only | Calls the Assembly hot path and its C fallback directly; scripts only see elapsed times (timing_and_files_test.lua `timing.*` covers the clock). |
| `hotpath.cycle_counter.reads_are_monotonic` | Internal only | Calls the Assembly hot path and its C fallback directly; scripts only see elapsed times (timing_and_files_test.lua `timing.*` covers the clock). |
| `hotpath.cycle_counter.assembly_and_fallback_advance_together` | Internal only | Calls the Assembly hot path and its C fallback directly; scripts only see elapsed times (timing_and_files_test.lua `timing.*` covers the clock). |
| `hotpath.cycle_counter.implementation_name_matches_selection` | Self-hosted | tests/js/self/cli_test.js.in `cli.version_line_in_every_spelling` (implementation name in the version line) |
| `hotpath.cycle_counter.assembly_is_in_use` | Internal only | Calls the Assembly hot path and its C fallback directly; scripts only see elapsed times (timing_and_files_test.lua `timing.*` covers the clock). |
| `lua.engine.registers_and_runs_tests` | Self-hosted | Every Lua script in tests/lua |
| `lua.engine.lua_errors_fail_with_traceback` | Partly | CTest prk_lua_failure_message; assertions_test.lua `messages_name_the_calling_line` |
| `lua.engine.reports_syntax_and_load_errors` | Self-hosted | CTest prk_lua_reports_syntax_error, prk_lua_reports_missing_file |
| `lua.sandbox.rejects_precompiled_bytecode` | Self-hosted | tests/lua/self/sandbox_test.lua `loads_text_but_not_bytecode` |
| `lua.sandbox.hides_ambient_access` | Self-hosted | tests/lua/self/sandbox_test.lua `hides_ambient_access`; tests/lua/prk_lua_api_test.lua |
| `lua.sandbox.enforces_instruction_limit` | Partly | CTest prk_lua_stops_infinite_loop (default limit); the custom limit sizes are C API only. |
| `lua.sandbox.enforces_memory_limit` | Self-hosted | tests/lua/self/sandbox_test.lua `memory_limit_is_a_catchable_failure`; tests/js/self/cli_test.js.in `runner.lua_memory_limit_fails_only_that_test` |
| `lua.sandbox.print_goes_to_output_callback` | Self-hosted | tests/js/self/cli_test.js.in `runner.print_stays_off_json_and_list_output` |
| `lua.api.add_test_only_while_loading` | Self-hosted | tests/lua/self/sandbox_test.lua `add_test_only_while_loading` |
| `lua.engine.rejects_invalid_arguments` | Internal only | NULL arguments to the C loading API. |
| `lua.engine.reports_versions` | Self-hosted | tests/lua/self/sandbox_test.lua `reports_versions`; tests/lua/prk_lua_api_test.lua |
| `scripts.detects_script_language` | Self-hosted | tests/js/self/cli_test.js.in `runner.script_problems_exit_4_or_2` (.txt is a usage error); CTest prk_mixed_lua_and_js_list |
| `scripts.loads_scripts_into_one_registry` | Self-hosted | CTest prk_mixed_lua_and_js_list; prk_self_sandbox_test (one Lua and one JS script in one run) |
| `scripts.runs_lua_scripts_from_command_line` | Self-hosted | Every CTest prk run of a script |
| `scripts.runs_js_scripts_from_command_line` | Self-hosted | Every CTest prk run of a script |
| `scripts.json_and_list_keep_print_off_output` | Self-hosted | tests/js/self/cli_test.js.in `runner.print_stays_off_json_and_list_output` |
| `scripts.reports_script_problems_with_exit_codes` | Self-hosted | tests/js/self/cli_test.js.in `runner.script_problems_exit_4_or_2`; CTest prk_lua_reports_* |
| `scripts.reads_script_files_from_disk` | Self-hosted | Every CTest prk run of a script |
| `scripts.tags_and_timeout_from_command_line` | Self-hosted | tests/lua/self/tags_test.lua; CTest prk_lua_timeout_option, prk_js_timeout_option |
| `scripts.host.data_path_policy` | Partly | tests/lua/self/timing_and_files_test.lua `files.refuses_paths_outside_the_working_directory`; exception containment is internal. |
| `scripts.host.applies_policy_and_contains_exceptions` | Partly | tests/lua/self/timing_and_files_test.lua `files.refuses_paths_outside_the_working_directory`; exception containment is internal. |
| `scripts.host.disk_services` | Self-hosted | tests/lua/self/timing_and_files_test.lua `files.*` |
| `lua.api.assertions_pass_on_good_values` | Self-hosted | tests/lua/self/assertions_test.lua |
| `lua.api.assertions_report_clear_failures` | Self-hosted | tests/lua/self/assertions_test.lua |
| `lua.api.failures_are_catchable_error_objects` | Self-hosted | tests/lua/self/assertions_test.lua |
| `lua.api.suites_name_tests_and_order_hooks` | Self-hosted | tests/lua/self/suites_and_hooks_test.lua |
| `lua.api.hook_failures_fail_the_right_tests` | Self-hosted | tests/lua/self/suites_and_hooks_test.lua |
| `lua.api.tags_and_name_filter_select_tests` | Self-hosted | tests/lua/self/tags_test.lua |
| `lua.api.skip_option_and_registration_errors` | Self-hosted | tests/lua/self/tags_test.lua |
| `lua.api.timeouts_stop_slow_tests` | Self-hosted | CTest prk_lua_timeout_option, prk_lua_stops_infinite_loop |
| `lua.api.clock_and_timing` | Self-hosted | tests/lua/self/timing_and_files_test.lua `timing.*` |
| `lua.api.file_checks_use_the_host` | Self-hosted | tests/lua/self/timing_and_files_test.lua `files.*` |
| `lua.api.file_reads_respect_memory_limit` | Internal only | Needs a tiny memory limit, which only the C API can set. |
| `cli.reporting.xml_escaping` | Self-hosted | tests/lua/self/reports_test.lua `junit.*` |
| `cli.reporting.junit_splits_test_names` | Self-hosted | tests/lua/self/reports_test.lua `junit.*` |
| `cli.reporting.junit_document` | Self-hosted | tests/lua/self/reports_test.lua `junit.*` |
| `cli.reporting.junit_reporter_times_tests_and_fans_out` | Self-hosted | tests/lua/self/reports_test.lua `junit.*` |
| `cli.reporting.command_line_writes_report_files` | Self-hosted | tests/lua/self/reports_test.lua (fixture runs with --json-file / --junit-file) |
| `cli.reporting.report_exit_codes` | Self-hosted | tests/js/self/cli_test.js.in `runner.report_file_errors_win_over_failures`; CTest prk_reports_and_exit_codes |
| `js.reports_engine_version` | Self-hosted | tests/js/self/sandbox_test.js `reports_versions`; tests/js/prk_js_api_test.js |
| `js.sandbox_has_no_ambient_access` | Self-hosted | tests/js/self/sandbox_test.js |
| `js.budget_cannot_be_caught` | Partly | CTest prk_js_stops_infinite_loop, prk_js_timeout_option; custom limits are C API only. |
| `js.timeout_and_memory_limits` | Partly | CTest prk_js_stops_infinite_loop, prk_js_timeout_option; custom limits are C API only. |
| `js.assertions_and_locations` | Self-hosted | tests/js/prk_js_api_test.js; CTest prk_js_failure_message |
| `js.async_tests` | Self-hosted | tests/js/prk_js_api_test.js; CTest prk_js_failure_message |
| `js.suites_hooks_and_tags` | Self-hosted | tests/js/prk_js_api_test.js; CTest prk_js_failure_message |
| `js.rejects_bad_registration` | Self-hosted | tests/js/self/cli_test.js.in `runner.script_problems_exit_4_or_2` (duplicate name) |
| `js.reads_only_granted_files` | Internal only | Needs a host with a custom file grant; no JS file test script yet. |
| `process.validates_requests` | Internal only | Pure C functions of the spawn layer. |
| `process.merges_environment` | Internal only | Pure C functions of the spawn layer. |
| `process.quotes_windows_arguments` | Internal only | Pure C functions of the spawn layer. |
| `process.bounds_captured_output` | Internal only | Pure C functions of the spawn layer. |
| `process.spawns_and_captures_output` | Self-hosted | tests/lua/self/process_test.lua, tests/js/process_test.js |
| `process.sets_environment_and_directory` | Self-hosted | tests/lua/self/process_options_test.lua |
| `process.feeds_standard_input` | Self-hosted | tests/lua/self/process_options_test.lua |
| `process.kills_process_on_timeout` | Self-hosted | tests/lua/self/process_options_test.lua |
| `process.reports_missing_executable` | Self-hosted | tests/lua/self/process_test.lua, tests/js/process_test.js |
| `process.limits_captured_output` | Self-hosted | tests/lua/self/process_options_test.lua |
| `process.reads_environment_variable` | Internal only | C helper of the spawn layer. |
| `script_process.caps_timeout_to_the_test` | Internal only | Needs a fake runner to observe the request. |
| `script_process.lua_passes_request` | Partly | tests/lua/self/process_test.lua `passes_arguments_exactly` |
| `script_process.lua_failures_are_clear` | Internal only | Needs a fake runner to observe the request. |
| `script_process.js_passes_request` | Partly | tests/lua/self/process_test.lua `passes_arguments_exactly` |
| `script_process.js_timeout_is_capped_by_test` | Internal only | Needs a fake runner to observe the request. |
| `script_process.scripts_spawn_real_processes` | Self-hosted | tests/lua/self/process_test.lua, tests/js/process_test.js, tests/js/self/cli_test.js.in |
| `c_driver.parses_gcc_diagnostics` | Partly | compile_test (real warnings and errors from the compiler on PATH); other formats are fake-runner only. |
| `c_driver.parses_msvc_diagnostics` | Partly | compile_test (real warnings and errors from the compiler on PATH); other formats are fake-runner only. |
| `c_driver.builds_compiler_arguments` | Internal only | Command lines, parsers, and the MSVC fallback through a fake runner. |
| `c_driver.formats_output_path` | Internal only | Command lines, parsers, and the MSVC fallback through a fake runner. |
| `c_driver.names_families_and_programs` | Internal only | Command lines, parsers, and the MSVC fallback through a fake runner. |
| `c_driver.compiles_through_runner` | Internal only | Command lines, parsers, and the MSVC fallback through a fake runner. |
| `c_driver.auto_falls_back_when_compiler_missing` | Internal only | Command lines, parsers, and the MSVC fallback through a fake runner. |
| `c_driver.rejects_invalid_requests` | Internal only | Command lines, parsers, and the MSVC fallback through a fake runner. |
| `c_driver.lua_compile_binding` | Self-hosted | tests/lua/self/compile_test.lua.in, tests/js/compile_test.js.in |
| `c_driver.js_compile_binding` | Self-hosted | tests/lua/self/compile_test.lua.in, tests/js/compile_test.js.in |
| `c_driver.compiles_real_program` | Self-hosted | tests/lua/self/compile_test.lua.in, tests/js/compile_test.js.in |
| `c_driver.builds_object_and_library_arguments` | Internal only | Command lines, parsers, and the MSVC fallback through a fake runner. |
| `c_driver.builds_link_arguments` | Internal only | Command lines, parsers, and the MSVC fallback through a fake runner. |
| `c_driver.names_output_kinds_and_paths` | Internal only | Command lines, parsers, and the MSVC fallback through a fake runner. |
| `c_driver.parses_msvc_discovery_output` | Internal only | Command lines, parsers, and the MSVC fallback through a fake runner. |
| `c_driver.discovers_msvc_through_runner` | Internal only | Command lines, parsers, and the MSVC fallback through a fake runner. |
| `c_driver.msvc_falls_back_to_discovery` | Internal only | Command lines, parsers, and the MSVC fallback through a fake runner. |
| `c_driver.missing_msvc_is_a_result` | Internal only | Command lines, parsers, and the MSVC fallback through a fake runner. |
| `c_driver.lua_compile_library_options` | Self-hosted | tests/lua/self/compile_test.lua.in, tests/js/compile_test.js.in |
| `c_driver.discovers_real_msvc` | Partly | compile_test on Windows without a Developer Prompt uses discovery; the direct check is C++ only. |
| `c_driver.builds_vcvars_arguments` | Internal only | Command lines, parsers, and the MSVC fallback through a fake runner. |
| `c_driver.decodes_utf16le_output` | Internal only | Command lines, parsers, and the MSVC fallback through a fake runner. |
| `asm_driver.builds_assembler_arguments` | Internal only | Command lines, parsers, and the MSVC fallback through a fake runner. |
| `asm_driver.chooses_family_by_extension` | Internal only | Command lines, parsers, and the MSVC fallback through a fake runner. |
| `asm_driver.parses_assembler_diagnostics` | Partly | tests/lua/self/assemble_test.lua.in, tests/js/assemble_test.js.in (real GAS / MASM / NASM output) |
| `asm_driver.assembles_through_runner` | Internal only | Command lines, parsers, and the MSVC fallback through a fake runner. |
| `asm_driver.masm_falls_back_to_discovery` | Internal only | Command lines, parsers, and the MSVC fallback through a fake runner. |
| `asm_driver.lua_and_js_assemble` | Partly | tests/lua/self/assemble_test.lua.in, tests/js/assemble_test.js.in (real GAS / MASM / NASM output) |
| `asm_driver.assembles_links_and_runs` | Partly | assemble_test assembles real objects; assemble-link-run is C++ only so far. |
| `update.hashes_sha256_vectors` | Internal only | Pure helpers and the command through fake HTTP; a real run needs the network, which tests never use. |
| `update.reads_json` | Internal only | Pure helpers and the command through fake HTTP; a real run needs the network, which tests never use. |
| `update.compares_release_versions` | Internal only | Pure helpers and the command through fake HTTP; a real run needs the network, which tests never use. |
| `update.reads_release_and_chooses_assets` | Internal only | Pure helpers and the command through fake HTTP; a real run needs the network, which tests never use. |
| `update.parses_sha256_files` | Internal only | Pure helpers and the command through fake HTTP; a real run needs the network, which tests never use. |
| `update.parses_update_and_version` | Self-hosted | tests/js/self/cli_test.js.in `cli.usage_errors_exit_2_with_usage_text`, `cli.help_texts`, `cli.version_line_in_every_spelling`, `cli.update_without_network_exits_6` |
| `update.reports_and_checks` | Internal only | Pure helpers and the command through fake HTTP; a real run needs the network, which tests never use. |
| `update.installs_verified_update` | Internal only | Pure helpers and the command through fake HTTP; a real run needs the network, which tests never use. |
| `update.refuses_unverified_update` | Internal only | Pure helpers and the command through fake HTTP; a real run needs the network, which tests never use. |
| `update.reports_lookup_failures` | Internal only | Pure helpers and the command through fake HTTP; a real run needs the network, which tests never use. |
| `update.version_reports_release` | Partly | tests/js/self/cli_test.js.in `cli.version_line_in_every_spelling` (offline line); the release line needs fake HTTP. |
